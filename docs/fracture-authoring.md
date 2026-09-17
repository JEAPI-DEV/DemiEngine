# Component-based fracture authoring

Author one ordinary scene or prefab. Add `Destructible3D` to the assembly root
and `Fracture3D` to each participating mesh. Prefer nested `children`; no second
recipe prefab, collider points, bond list or part-to-visual map is required.

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
The former **Create fracture prefab** dialog is removed. Existing top-level
recipe documents remain readable for compatibility, but new examples and editor
workflows use components.

## Settings and behavior

- `Destructible3D.seed` defaults to 1; `generator_version` defaults to 1.
  `max_bodies` defaults to 64 (maximum 256).
- `Fracture3D.pieces` defaults to 8 (1–128 per mesh, at most 256 per assembly).
  One piece participates in the structure but stays whole when detached.
- `bond_health` defaults to 1. Connections use the lower of the two objects'
  strengths. This is not a material-density or structural-load model.
- `anchor_below` is an optional assembly-local Y threshold. Omit it or use null
  for no foundation attachment; in the Inspector choose Set/None. Direct hits
  can break attachments. Initial attachment health is the strongest incident
  bond health, or 1 for a singleton.
- Optional normalized RGBA `interior_color` defaults to
  `[0.35, 0.33, 0.30, 1]`; `interior_material` references a material asset.

An optional root Rigidbody3D supplies mass and other body settings. Omitted mass
uses the canonical Rigidbody3D default; generated body type is static with anchors, dynamic without.
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
directly in scenes. Packaged runtime does not execute the splitter. There is no
persistent authoring cache or live damage migration across source changes yet.
Generated `Destructible3D.parts` and `ModelCollider3D.inline_geometry` are low-level
transport, not normal authoring fields.

## Current limits

Box primitives, inline triangle meshes and closed convex static glTF/GLB geometry
are supported. Open, concave, skinned and overly complex inputs fail explicitly.
Seeded convex plane bisection generates closed interior faces and contact-derived
bonds, not general runtime CSG. Limits are 1,024 faces / 512 unique source vertices,
256 hull vertices and 2,048 bonds. Generation is synchronous.

Imported models require explicit exterior material/texture/color settings.
Embedded multi-material extraction and authored smooth normals are not preserved;
output normals are flat. The click probe still uses part-local damage. Spatial
radius/energy hits, runtime refinement, structural stress, fair scheduling, debris
budgets and production/platform performance qualification remain roadmap work.
