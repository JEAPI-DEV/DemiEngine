# Component-based fracture authoring

Author one ordinary scene or prefab. Add `Destructible3D` to the assembly root
and `Fracture3D` to each participating mesh. Prefer nested `children`; no second
recipe prefab, collider points, bond list or part-to-visual map is required.

For repeated walls, prefer compact `Masonry3D` regions and proximity activation
instead of authored brick arrays. Height-map relief is generated in memory, not
exported into asset folders. See [streamed destruction](streamed-destruction.md).

```json
{
  "format_version": 1,
  "id": "prefab://wall",
  "entities": [{
    "id": "wall",
    "components": {
      "Transform3D": {},
      "Rigidbody3D": { "mass": 1000 },
      "Destructible3D": {}
    },
    "children": [{
      "id": "concrete",
      "components": {
        "Transform3D": { "position": [0, 1.5, 0] },
        "MeshRenderer": { "shape": "cube", "size": [4, 3, 0.4] },
        "Fracture3D": { "pieces": 16, "anchor_below": 0.01 }
      }
    }, {
      "id": "reinforcement",
      "components": {
        "Transform3D": { "position": [0, 1.5, 0] },
        "MeshRenderer": { "shape": "cube", "size": [0.1, 2.9, 0.12] },
        "Fracture3D": { "pieces": 1, "bond_health": 10, "anchor_below": 0.06 }
      }
    }]
  }]
}
```

For a standalone mesh, place both components on that entity. Meshes without
`Fracture3D` remain ordinary objects. Participating meshes belong to their nearest
`Destructible3D` ancestor (or themselves). Physical assembly roots must currently
be unparented; nested physical assemblies and cross-prefab participation are not
supported. Transform-only grouping beneath an assembly is supported.

## Editor workflow

Open the ordinary prefab, add **Destructible 3D** to its root, then add
**Fracture 3D** to its mesh objects using Add Component. Edit the settings in the
Inspector with normal Undo/Redo and Save. The viewport retains the original
editable objects; Play mode loads the generated fracture geometry. A separate
generated-shard preview toggle is not implemented.

Adding an assembly before its first fracture mesh is allowed. An assembly with
no participating meshes is inert, so component authoring can happen incrementally.
The former **Create fracture prefab** dialog and top-level `fracture` recipe
format are removed. Prefabs use `Destructible3D` with `Fracture3D` meshes or
`Masonry3D` regions. Ordinary nested prefabs and their overrides compose these
components before generation.

Nested prefab overrides are applied before fracture generation, including when
cooking. A prepared prefab retains its compact recipe in build output so runtime
overrides can regenerate geometry when needed. Do not edit that generated
`source_recipe` field. Change the authored prefab and rebuild instead.

## Settings and behavior

- `Fracture3D.collider` defaults to `source`. Set `box` with `pieces: 1` to keep
  a detailed model/inline mesh intact while using an explicit unit-box proxy
  scaled by `MeshRenderer.size` and the entity transform. The model must be
  authored in that unit box. Relief does not enter collision or density-derived
  mass. Its UVs, normals, materials and LOD references remain on the detached
  visual. This is an explicit approximation, not automatic concave decomposition;
  subdividing the detailed visual during fracture is not supported.
  `SurfaceRelief3D` also works on this explicit single-piece proxy path; its
  renderer generates the visual mesh from a height map at runtime.
- `Destructible3D.seed` defaults to 1; `generator_version` defaults to 1.
  `max_bodies` defaults to 64 and accepts positive signed 32-bit values.
- `Destructible3D.energy_per_health` defaults to 1000 joules per authored health
  unit for [spatial impacts](3d-spatial-impacts.md). It does not change `damage_part` units.
- `Fracture3D.pieces` defaults to 8 and accepts positive signed 32-bit values.
  One piece participates in the structure but stays whole when detached.
- `bond_health` defaults to 1. Connections use the lower of the two objects'
  strengths. This is not a material-density or structural-load model.
- `density` is optional, in kg/m³ (0.001–1,000,000). When any participating mesh
  supplies it, omitted/null densities on the other meshes use 1000 kg/m³.
  Without an explicit root mass, the compiler calculates total mass as the sum
  of each solid's volume times density, including authored root scale. For
  example, use 2400 for concrete and 7850 for solid steel. A hollow door modeled
  by a solid collision box needs **effective density** (desired mass divided by
  that box's volume), not the density of solid steel. Empty space is not inferred.
  Density does not change bond health or fracture resistance.
- `anchor_below` is an optional assembly-local Y threshold. Omit it or use null
  for no foundation attachment; in the Inspector choose Set/None. Direct hits
  can break attachments. Initial attachment health is the strongest incident
  bond health, or 1 for a singleton.
- Optional normalized RGBA `interior_color` defaults to
  `[0.35, 0.33, 0.30, 1]`; `interior_material` references a material asset.

An optional root Rigidbody3D supplies body settings. An explicit `mass` overrides
the calculated total, but per-part density still controls its distribution.
With no density authored, the previous uniform distribution and canonical
Rigidbody3D mass default remain. Split groups receive the sum of their parts'
mass shares; Jolt uses the same densities for center of mass and inertia.
Authored scale is accounted for at compilation; runtime resizing does not
automatically change mass. Overlapping solids count both volumes (reinforcement
does not subtract a void from concrete). Generated body type is static with anchors, dynamic without.
For generated assemblies, this replaces an authored static/dynamic body type;
configure foundation anchors instead. Kinematic assemblies are rejected. The compiler generates the
collider: do not add another root collider or independent physics to participating
mesh children.

Original entity IDs, transforms and gameplay components remain as assembly/source
handles at runtime. Participating source renderers are replaced by derived shard
visuals, which follow the native split bodies. Scripts/attachments on original
source handles do not automatically follow an individual shard; query
`Destruction3D.state` for current physical owners. Authored source is never
replaced with generated vertices or mappings.

## Generation and cooking

```sh
demi asset fracture examples/fracture_prefab_lab/prefabs/wall.prefab.json
demi run --project examples/fracture_prefab_lab
```

The CLI checks generation without modifying source. Prefab overrides are applied
before generation. Instance placement/IDs do not change the local fracture pattern;
instances have independent physical ownership and damage.

Runtime source loading and cooking share the compiler. Cooking bakes components
to prepared geometry under the same prefab identity, including components authored
directly in scenes. For ordinary source-mesh fracture, packaged runtime does not
execute the splitter unless authoring overrides require regeneration. Cooking
also prepares `Masonry3D` geometry and retains compact component source in
`source_recipe` for overrides. Uncooked source generates on instantiation;
repeated instances use the bounded session template cache.
There is no persistent disk authoring cache or live topology migration across
source changes. Optional validated damage checkpoints are described in the
streaming document above.
Generated `Destructible3D.parts` and `ModelCollider3D.inline_geometry` are low-level
transport, not normal authoring fields.

## Current limits

Component-authored meshes with multiple pieces now retain their source renderer
while all their pieces share one body. Their generated outer and interior
surfaces remain deferred until the source splits across bodies. The source
entity keeps its identity and gameplay components when its intact renderer is
removed. A standalone assembly/mesh gets an internal `root-id/intact` visual;
that ID must not collide with an authored entity.

Nested prefabs and overrides use this activation path too. Rejected splits do
not publish shard entities; checkpoint restoration refines only affected
sources. This does not change template-cache eligibility, unload cold geometry,
or add further subdivision after the first source-level refinement. Custom masonry models use
[intact instance batches](mesh-instances-3d.md) until their region splits.

Intact models use the authored renderer; generated shards retain the existing
fracture material/normal limitations below. This change does not establish
appearance parity for embedded materials or smooth shading.

Box primitives, inline triangle meshes and closed convex static glTF/GLB geometry
are supported. Open, concave, skinned and overly complex inputs fail explicitly.
Seeded convex plane bisection generates closed interior faces and contact-derived
bonds, not general runtime CSG. Each collision hull remains limited to 256
vertices by Jolt. The separate source-face, source-vertex and bond count caps
have been removed. Generation is synchronous, so large inputs can take time
and substantial memory; removing a validation cap is not a performance guarantee.

Imported models require explicit exterior material/texture/color settings.
Embedded multi-material extraction and authored smooth normals are not preserved;
output normals are flat. The click probe uses native spatial strikes and radial
blasts. Richer material response, runtime refinement, structural stress, fair scheduling, debris
budgets and production/platform performance qualification remain roadmap work.
