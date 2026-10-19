# Terrain Milestone 5: water integration

Status: completed for the scoped Linux/Vulkan integration on 2026-10-17.
This is a focused qualification, not a full repository or cross-platform gate.

## Shared authoring and gameplay boundary

Water Body supports finite simple polygon boundaries for lakes and oceans,
and optional Catmull-Rom river control paths sampled once during generation.
Linear paths remain the default; gameplay queries consume prepared segments.
Basin Water proposes connected lakes from measured priority-flood spill levels;
proposal identities use node IDs and stable sink grid coordinates. Water Edit
excludes or patches bodies by stable ID without carving the terrain again.
The shared authoring parser validates merged overrides, and runtime coverage,
queries and rendering use the same resolved ownership and triangle heights.

Prescribed currents are independent of drainage accumulation. Follow-bed rivers
retain their authored downstream XZ heading, follow the actual surface triangle
tangent and normalize to the prescribed terrain-local speed. Runtime queries
transform velocity by terrain scale and rotation; normals use inverse-transpose
scale. Water columns follow terrain-local up rather than the sloped normal.

`demi.gameplay.water` exports opt-in buoyancy and swimming Lua modules. They
apply fixed-step forces to enabled dynamic bodies and preserve native contacts.
Buoyancy approximates a virtual segment, rather than integrating collider hull
volume or torque. Swimming policy, input conversion, animation, gravity and land
controller transitions remain game responsibilities.

## Rendering ownership

`OpaqueSceneCapture3D` owns per-camera immutable opaque colour and single-sample
linear-depth capture targets. The depth prepass is independent of the main
scene's 4x MSAA. `WaterSurfaceRenderer3D` consumes prepared surface geometry,
body appearance, current and the camera capture. Camera immersion comes from
shared gameplay water queries; the post-process owner consumes underwater
appearance and captured scene depth.

Water optics combine opaque-scene refraction, depth-dependent absorption,
Fresnel environment reflection, directional highlights, animated shading
normals and a shallow-depth foam band. Wave settings do not displace geometry
or change queried surfaces. Reflection is environment-based rather than
screen-space or planar object reflection. Transparent objects and other water
surfaces are outside opaque refraction capture. Custom shader materials lack a
supported capture-depth pass and produce an explicit diagnostic.

Prepared terrain generator version 13 and binary payload version 10 require
regeneration and recooking. Generated resources remain separate from authored
entity data. Retained dropped body IDs and resolved coverage indices must
survive payload round trips without reintroducing invalid geometry.

## Qualification

Release CLI/editor builds succeed. Twenty-five focused suites pass: shader interfaces,
renderer batching, capture allocation/lifetime, underwater camera selection,
water optics/state, Lua/physics gameplay, water meshes/world/query/runtime,
terrain authoring/graph/drainage/cooking/materials/assets, Lua water bindings and
editor graph/history, shared GPU resources/commands, 2D rendering and scene
extraction. Added river-curve and complete Basin Water -> Water Edit
regressions also pass. Both GLSL/ESSL and SPIR-V water shaders compile; the
nineteen linked shader interfaces and texture contracts are checked.
All 33 repository examples validate. This is not the full repository test suite.

`examples/water_interaction_3d` contains a sloping inlet feeding a lake, a
buoyant gravity-driven body, a swimming dynamic body and a dry control.
Headless and visible Vulkan E2E runs pass (1 passed, 0 failed). The probe checks
stable river identity/current direction, flotation, swimming movement and
unchanged dry/gravity policy. Native Lua/physics tests also cover current-relative
drag, disabled bodies/terrain, timestep composition and swimming eligibility.
Four pure Lua package tests and local/offline installation checks pass.

The example cooks for Linux and its cooked project validates and renders.
Payload tests cover body appearance/current metadata, rejected authored bodies,
query geometry and stable IDs. Source and cooked native captures were visually
checked; animated normals and moving bodies mean these captures are not claimed
pixel-identical. A final inlet fixture is retained in the example, not in a
hand-edited cooked payload.

Native runs used one visible process at a time and no concurrent build. The
X11 host reported Vulkan with 4x MSAA. Actual captures were 3840 x 2022 (the
window host clamps to the selected monitor), with a separate near-surface
capture at 5120 x 2806. Source captures:
`/tmp/m5-water-resolved.png`, `/tmp/m5-water-inlet.png`,
`/tmp/m5-underwater-final.png`, `/tmp/m5-water-exit.png`;
cooked captures: `/tmp/m5-water-cooked.png` and `/tmp/m5-final-cooked.png`.
A temporary probe using the repository's existing evening environment panorama
verifies actual environment sampling (`/tmp/m5-water-environment.png`); it does
not add that external art to the gameplay package.
The rocky valley also renders with its real gray-cliff materials and the new
water path: `/tmp/m5-rocky-water.png`.

The native checks found and corrected issues not covered by isolated mocks:
texture stage zero belongs to the primary binding, camera target offsets need
normalization for eye depth, instancing diagnostics must not overwrite depth
camera data, and opaque MSAA colour must resolve before snapshot copying.
An explicit alternate-target transition before the copy removes prior-frame
feedback and moving-object trails. Regression tests preserve batch camera data;
source/native evidence verifies the actual backend resolve order.

## Warm performance

NVIDIA GeForce RTX 3070 Ti Laptop, Linux Release/Vulkan, 3840 x 2022,
4x MSAA, performance-debug overrides off. Exclude frames before 200 from each
600-frame capture. All retained focus samples are 1.

| Workload / GPU scope | Median | p95 |
|---|---:|---:|
| Rocky Valley total GPU | 6.779 ms | 7.648 ms |
| Rocky Valley opaque main | 4.759 ms | 5.279 ms |
| Rocky Valley depth capture | 0.345 ms | 0.454 ms |
| Rocky Valley colour capture | 0.023 ms | 0.025 ms |
| Rocky Valley water/transparent | 0.206 ms | 0.212 ms |
| Inlet/lake gameplay probe total GPU | 2.073 ms | 2.364 ms |
| Underwater gameplay probe total GPU | 2.753 ms | 2.918 ms |

Traces: `/tmp/m5-rocky-water.csv`, `/tmp/m5-water-inlet.csv`,
`/tmp/m5-underwater-final.csv`. Startup preparation/compilation remains separate;
these are warm measurements, not a hardware limit or general FPS guarantee.
GPU pass intervals overlap other pipeline work and must not be added to CPU
frame times. Frustum/mask rejection avoids capture work when no water is visible;
an underwater camera retains its optical capture independently of surface culling.

## Limits and publication

Shoreline shape follows terrain sampling; the rocky valley's coarse river banks
remain visible. This milestone does not claim realistic fluid simulation,
geometric waves, scene-object reflections, boat-hull buoyancy or a full character
animation rig. Refraction captures opaque geometry only. Normal-only animation
keeps rendered base surfaces and gameplay queries consistent. Unsupported texture
copy/R32F capture or custom material depth programs produce explicit diagnostics.
Physical Android and Windows native qualification remain open.

`demi.gameplay.water@1.0.0` is published through the package store. A fresh public
installation with an empty cache validates and passes the example's headless
E2E probe. The repository example keeps its relative local registry for development.

Swimming eligibility is entirely package-owned. The obsolete native
`swimmable` field and hard-coded depth/current thresholds were removed; native
queries report physical facts only. Focused river-route/query tests now compare
those facts and vectors rather than duplicating gameplay policy.

The maintained terrain guide is deployed in store release
`20261017-terrain-m5-water`. Composer tests, all 106 Twig templates and production
cache warmup passed before activation; the health endpoint, live curve guidance
and gameplay package version were checked after deployment.

The final gate also covers cached authored camera targets: an unchanged target
presents its completed image without reprocessing shared scratch data from
another camera. Underwater optical state is evaluated only when content updates.
The full 25-suite focused gate passes after this fix and native policy cleanup.


## Low-angle visual follow-up

The initial overhead acceptance missed an incorrectly authored inlet. Its
water profile stood above adjacent basin ground, and carving cannot raise
retaining banks. The gameplay example now leaves a longer upstream bank, routes
the descending profile through that bank, and reaches lake level at the shore.
The dry control retains its physics test but no longer has a visible mesh;
all probes have descriptive hierarchy names.

Default and near-Full LOD captures plus a normal-debug view confirmed mesh
contact in the lake fixture. The apparent separation was reinforced by the
opaque optical rim and dark bank shading. Contact coverage now blends the
complete optical result over the undistorted opaque scene within one pixel of
zero depth; geometry and gameplay elevations are not moved. The example also
uses a distinguishable wet-bank biome, calmer lake foam and outdoor fill light.

A second join issue was a per-body change of ripple direction and phase. Normal
waves now share terrain-local coordinates and clock for matching wave settings;
foam still advects with each body's current. This removes the artificial ribbon
inside the coplanar lake without changing its current queries.

Generation now warns about low lateral river banks using the body's local
surface profile. It permits open flow endpoints and joins with wet bodies.
Tests cover contained and exposed sloped channels, local-level comparisons,
open ends and receiving water. The warning does not change authored elevations
or add invented retaining walls.

The follow-up Release CLI/editor and six focused shader, water, graph and asset
suites pass. The updated example validates, its headless gameplay E2E reports
1 passed/0 failed, and the Linux cooked project validates. Fresh native captures
check the corrected inlet from the side and the lake from a low angle:
`/tmp/water-contact-overview.png`, `/tmp/water-contact-final.png` and
`/tmp/water-joined-final.png`. No terrain or water height was shifted in the
renderer to conceal contact errors. These checks replace the inadequate
overhead-only acceptance for this example.

The updated terrain guide was deployed as `20261017-water-shore-contact` after
Composer tests, 106 Twig template checks and production cache warmup. The live
health endpoint and river containment guidance were verified.
