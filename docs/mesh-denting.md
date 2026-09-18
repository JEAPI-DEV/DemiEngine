# Native mesh denting (experimental)

Denting is explicitly optional. Add **Dentable 3D** from the editor's Add
Component menu, or put `"Dentable3D": {}` in an entity/prefab's `components`.
Entities without it reject both `MeshDeformation.dent` and `.impact`, including
zero-energy requests. Removing the component clears damage and restores normal
rendering on the next update.

The component owns per-instance dent state and the optional `radius`,
`yield_energy`, `stiffness`, `absorption`, and `max_depth` material settings.
The Inspector displays their native defaults; `.impact` uses these settings
unless its optional material argument overrides individual values. A
`MeshRenderer` is required; animated entities are not supported yet.

Adding the component alone performs no subdivision and creates no private GPU
mesh. Those costs are paid only after an impact/dent request. Ordinary entities
have no dent-state vector and retain normal instancing. Shared CPU model source
geometry is still cached once per loaded model, not per entity. The component
permits dents; the existing collision handler/API decides when to apply them.

`MeshDeformation.dent` records a permanent visual impact on one entity. C++
deforms the mesh with a smooth spherical brush and recomputes surface normals;
Lua does not manipulate vertices. Static GLB/glTF models (including those on
dynamic rigidbodies) and procedural triangle meshes are supported. Source
geometry, UVs, vertex colors, materials, and other instances stay intact.

For physical hits, use the native energy-based entry point with a collision
event, rather than choosing a fixed depth:

```lua
local MeshDeformation = require("demi.mesh.deformation")

-- Inside a physics3d_collision_enter handler, for the entity being dented:
local ok, error, depth = MeshDeformation.impact(contact.entity_id, contact, {
  radius = 0.32, yield_energy = 4, stiffness = 4000,
  absorption = 0.7, max_depth = 0.15,
})
```

Jolt contacts expose `impact_energy` in joules, captured before the solver
changes velocities. It is `0.5 * effective_mass * normal_closing_speed^2`.
Effective mass is the reduced translational mass of the two dynamic bodies;
static/kinematic bodies have zero inverse mass. Point velocities include angular
motion. Grazing/separating impacts contribute less/no normal closing energy.
This is an impact-energy estimate, not a complete rotational energy or stress
solver. Trigger contacts report zero.

The native material response uses
`depth = sqrt(2 * max(absorption * energy - yield_energy, 0) / stiffness)`,
capped by `max_depth` and half the brush radius. `stiffness` is an effective
N/m resistance, not a measured metal Young's modulus. Force affects velocity
through physics; it is not added again as another damage source. Consume **enter**
events so resting contacts do not repeatedly damage a mesh.

```lua
local MeshDeformation = require("demi.mesh.deformation")

local ok, error = MeshDeformation.dent("barrel", {
  point = {hit_x, hit_y, hit_z},
  direction = {travel_x, travel_y, travel_z},
  radius = 0.32,
  depth = 0.12,
})
MeshDeformation.reset("barrel") -- Restore the original appearance.
```

Point and direction are world-space; radius and depth are meters. Direction
points inward, along the impact, and need not be normalized. Hierarchy transforms,
nonuniform scale, and mesh size are accounted for. The dent follows the entity
after impact. `dent` returns `(accepted, error)`; acceptance records a brush and
does not guarantee overlap with the surface. The engine automatically subdivides
the private mesh when its triangles are too coarse for the brush. No alternate
model or Blender preparation is required. The bounded first implementation uses
up to three uniform subdivision levels and stops increasing a mesh before it
would exceed 200,000 triangles; already larger source meshes are not decimated.
Very small brushes can reach this budget and still have limited detail.

Depth must be positive and at most half the radius. At most 32 dents are retained
per entity; further requests return an error without changing existing dents.
Repeated dents accumulate. Reset clears them. Animation/skinning and built-in
low-resolution primitives are not supported in this first version.

## Physics and lifetime

This is visual plastic deformation, **not soft-body simulation**. Collision,
mass, inertia, and rigidbody state remain unchanged. Gameplay damages an opted-in
entity by forwarding an actual collision event to `MeshDeformation.impact`;
energy measurement, material response, refinement, and deformation are native.
Large dents can visibly diverge from the original collider. No fracture,
penetration, volume preservation, spring response, or structural-strength model
is implied. Dents are runtime-only: not serialized into scenes/prefabs, replicated,
or persisted in savegames automatically. Scene reload restores source geometry.

## Rendering and costs

Loaded model rest geometry is retained once on the CPU. Only dented instances
allocate private GPU geometry and leave the shared-model instancing batch.
Unchanged dents reuse those buffers. Rebuilding includes bounded subdivision,
refined vertex count × dent count, normal generation and upload, and occurs only
after dent/source changes.
Destroyed/reset entities release private caches on the next rendering update,
even offscreen. Asset reload clears caches. Dented instances stay on their
high-detail model until reset so LOD switching cannot erase damage. Inline mesh
culling bounds expand conservatively with deformation.

Thousands of simultaneously dented objects are not qualified. Bound the number
of damaged objects and requests; profile impact frames separately from steady state.

## Demo

Run `demi run --project examples/performance_3d_lab` and press **D** or choose
**Mesh Denting**. Two barrels share one model. The left is the untouched reference;
click the right barrel or press **Space** to fire a real sphere at it. **M** changes
projectile mass and **V** changes speed. The HUD reports measured impact energy
and native dent depth. **R** restores the mesh and **B** returns to the lab.
Both barrels use the existing benchmark model unchanged. The map remains
editable in `scenes/denting.scene.json`.
