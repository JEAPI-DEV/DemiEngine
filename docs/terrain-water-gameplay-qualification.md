# Terrain water gameplay queries

## Ownership

`TerrainWaterRuntime` adapts immutable generated water data to world-space
queries. It enumerates loaded terrain owners, not their generated mesh entities.
Query contexts are retained per shared heightfield; placements of one asset
reuse the context. Contexts are created only for queries inside the local XZ
bounds and released when their terrain owners unload, even if an asset cache
still retains the heightfield. Sampling never regenerates terrain or scans
colliders.

`TerrainWaterTracker` is a UI-free transition state machine in the lightweight
water library. Lua bindings convert its values into tables. A tracker compares
scene, terrain placement and water-body identity, returning exit before enter
when the immersed body changes. Stay is returned on every explicit update in
the same body. Reset clears state without emitting an exit.

`demi.terrain.water` belongs to the main-thread gameplay VM. Worker VMs do not
gain direct mutable-world access. The optional example sensor uses ordinary
Lua event-bus messages; no automatic global listener or native actor scan is
installed. Swimming, splashes, damage and buoyancy remain game policies.

## Coordinate contract

Input positions and output surface points/normals are world-space. The query
projects along terrain-local up, including rotated placements, rather than
casting a global vertical ray. Depth is bed-to-surface distance along that axis
in world units. A wet column may return a sample for a point in air or below the
bed, with `underwater=false`. Bed inclusion is inclusive and the water surface
is exclusive. Dry or unavailable water returns nil.

Enabled hierarchy, placement transforms, regeneration and unloading are read
at the next explicit query. Overlapping terrains prefer an immersed sample,
then the nearest surface. This is point containment, not volume intersection or
fluid simulation. The shared inverse-transform helper now accepts every nonzero
scale instead of silently collapsing scales below an arbitrary epsilon; water
queries reject nonfinite transformed results and skip zero-scale placements.

`Transform3D.get_world_position` uses the existing hierarchy resolver so sensor
scripts do not confuse parent-relative position with world position.

## Qualification

Release CLI and editor builds succeed. Eleven focused checks pass: hierarchy,
Lua stub contract, water world publication, native water runtime, Lua water,
water queries, terrain graph, world update, generator, surface and terrain asset.
Tests cover shared-field cache reuse, terrain filters, translated/scaled/rotated
placements, tiny nonzero scales, disabled owners, regeneration, scene identity,
unload cleanup, ordered transitions, invalid Lua arguments, parented actors and
the actual optional sensor script through the host lifecycle and event bus.

The terrain graph example validates without diagnostics. Source and fresh cooked
three-frame headless runs are checked separately. No full-suite, Android device,
interactive graphics, underwater post-processing or automatic buoyancy
qualification is claimed. No terrain asset-format change is required.
