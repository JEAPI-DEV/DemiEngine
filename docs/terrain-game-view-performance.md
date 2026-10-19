# Dense-terrain Game View performance

This follows the [viewport qualification](editor-viewport-navigation-qualification.md).
The reported problem is still excessive CPU time in `examples/terrain_3d`, with
embedded Play slower than the authored viewport. Scene density, materials,
shadows, draw distance and simulation time are preserved.

## Baseline

The October 18 baseline used the Linux Release editor, Vulkan, a 5120 x 2806
window, one engine process and no concurrent build. Embedded Play was started
through the toolbar. A CSV export fix was needed because the previous editor
trace stopped writing when Play began. The baseline already includes the first
presentation-local transparency-cache change, so it is not the old unmodified
renderer.

After excluding initialization, 968 embedded frames had these median CPU times:

| Scope | Median |
| --- | ---: |
| Complete editor frame | 102.03 ms |
| Editor UI | 15.36 ms |
| Runtime update | 40.25 ms |
| Fixed simulation, six ticks per frame | 38.76 ms |
| Physics2D, six ticks | 10.85 ms |
| Physics3D, six ticks | 26.48 ms |
| Render submission | 46.26 ms |

There are 23,848 native entities but only 346 physics bodies. The camera sees
4,965 mesh records across render views, with 66,725 culled. Physics and runtime
hierarchy presentation were repeatedly scanning or submitting the whole entity
population. Slow frames also require more fixed ticks, multiplying those scans.
Scopes are nested; child costs must not be added again to enclosing totals.

## Shared changes

`EntityComponentQuery` retains ordered component membership, not component values
or pointers. Physics fetches current components and enabled state from each
matching entity. Structural changes invalidate through `ComponentStorage`, and
collection address/size changes also rebuild queries. Observed storage destruction,
assignment and moves cover same-slot replacement, swaps and vector reordering.
Queries observe nonmatching and empty slots as well as matching ones. Temporary
parser/default-value storage is unobserved and does not invalidate live-world
queries. Query use and structural mutations remain serialized; the epoch does not
make world mutation concurrently safe. Mutations in another observed world may
conservatively invalidate a cache.

Physics2D queries 2D transforms and joint types, retaining mixed-dimension support.
Physics3D queries collider/destructible membership and character controllers.
Lighting uses the same query mechanism with renderer-owned caches. No fixed ticks
are discarded and no collider/physics feature is skipped based on project dimension.

`MeshInstances3DComponent` owns its stable-label map behind const access and explicit
set/erase/clear operations. A content token identifies a complete value snapshot;
copies preserve it, and mutations receive new tokens. Native field serialization
still uses the same JSON object and labels. Renderer copies can therefore retain
transform math without traversing unchanged authored maps. Bounds remain dependent
on resolved model bounds, deformation expansion, mesh size and wind; those input
values are checked before reusing cached copy bounds.

Surface transparency is classified once per prepared source/section per presentation,
while transparent depth sorting remains camera-specific. Terrain LOD section mappings
have separate decisions. Ordinary visible records bypass the extra section-expansion
copy when it is unnecessary. Resource-retention checks reuse prepared component data.

The read-only runtime hierarchy clips offscreen ImGui rows, preserving scrolling,
labels and selection. This changes presentation work, not the runtime entity set.

## Profiling workflow

```sh
demi editor --project examples/terrain_3d \
  --max-seconds 120 --profile-frames /tmp/terrain-game.csv
```

Start Play in Game View. `Editor.playing` distinguishes authored and runtime frames.
The editor starts the embedded profiler frame before UI dispatch, so UI and GPU
samples are not erased by the subsequent simulation update. Standalone users of
`EmbeddedRuntimeSession` still get a fresh profiler frame from update/step when
the host did not begin one. The CSV includes `Editor.frame`, UI, simulation and
render scopes. Fixed-update call counts expose catch-up work.

Use the Release editor and relaunch after rebuilding. Do not measure while another
engine instance or compiler is running. Keep the test window visible and the desktop
awake. Compare the same camera, draw counts and resolution; profiler screenshots or
GPU timings from inactive windows are not enough to establish a speedup.

## Final checks and standalone measurements

The 24-test Debug gate passed for component queries/storage, native reflection,
physics (including destruction and contacts), transforms, rendering, visibility,
Play lifecycle/profiling, runtime hierarchy clipping, terrain and Lua behavior.
The three affected renderer tests passed again after sparse-source preparation.
The six rendering/debug/particle/water tests passed after the final empty-pass
changes. This is scoped verification, not a claim of running all 448 tests.

The Release CLI, runtime and editor were rebuilt. `terrain_3d`, `rocky_valley_3d`,
`minimal_3d`, `performance_3d_lab` and `isometric_base_builder` validated without
diagnostics. Bounded 2D and 3D headless runs exited successfully. A separate native
visual run was captured after frame 30 and confirms terrain and instances render.

GNOME input permission expired before the final scripted Play click. Consequently,
the **final embedded Game View comparison remains pending**. The 102.03 ms embedded
baseline above must not be compared directly with the standalone numbers below as
an exact editor speedup ratio.

The standalone checks used the rebuilt Release CLI, Vulkan/X11, 3576 x 2142,
`examples/terrain_3d`, the unchanged authored camera, and no concurrent editor or
build. The final run recorded 300 frames; the first 30 were excluded. It retained
346 bodies and 115,772 submitted triangles, matching the preceding standalone
probe. Two fixed ticks per presentation were typical; simulation ticks were not
removed or capped by these changes.

| Scope | Final median | Final p95 |
| --- | ---: | ---: |
| Total frame | 26.42 ms | 27.39 ms |
| Runtime update | 2.53 ms | 3.03 ms |
| Physics2D, all ticks | 0.014 ms | 0.021 ms |
| Physics3D, all ticks | 0.36 ms | 0.42 ms |
| Render preparation/submission | 23.61 ms | 24.50 ms |
| Source preparation | 6.22 ms | 7.09 ms |
| Visible extraction, all views | 7.73 ms | 8.74 ms |
| Mesh submission, all views | 5.81 ms | 6.22 ms |
| Debug geometry | 0.002 ms | 0.003 ms |
| World text | 0.001 ms | 0.001 ms |
| GPU | 5.54 ms | 6.16 ms |

Before removing empty-pass transform traversal, the otherwise equivalent standalone
probe measured 32.73 ms total and 30.14 ms rendering, with the same bodies and
triangles. Debug geometry now queries only requested overlay types or individually
visible collider types; world text queries only text owners. Both filter before
resolving transforms. Particle and water-capture checks likewise avoid scanning
nonparticipants. Their rendering behavior, masks and geometry remain covered by
focused tests.

**Remaining limit:** 26.42 ms still exceeds a 60 Hz frame budget. Source preparation,
visibility extraction and submission remain CPU costs worth investigating; the
GPU is not the dominant limit in this probe. No final native editor FPS claim is
made without the pending embedded/moving-view check.

Evidence files from this session:

- `/tmp/game-performance-gate-tests.log`, `/tmp/game-sparse-tests.log`,
  `/tmp/game-empty-passes-tests.log`;
- `/tmp/terrain-game-before.csv` and its before/after screenshots;
- `/tmp/terrain-runtime-final.csv` (before empty-pass fix),
  `/tmp/terrain-runtime-qualified.csv` (final timing probe);
- `/tmp/terrain-runtime-qualified.png` (separate visual check after frame 30).

These temporary files are local qualification evidence, not packaged game assets.
