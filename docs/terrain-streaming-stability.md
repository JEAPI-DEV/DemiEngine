# Regional terrain streaming stability and embedded Play CPU work

2026-10-31, Linux Release, `examples/terrain_3d` Alpine scene.

## Diagnosed problems and changes

- Regional CPU geometry was published before its asynchronous GPU heightfield
  and mapped-material bundle were ready. That briefly selected ordinary biome
  meshes, then switched to GPU surface blending. Height morphing alone could
  not hide this shading change.
- `RegionalTerrainPresentation` now carries immutable replacement requests and
  a renderer readiness acknowledgment. Preparation culls requests against the
  camera, retains the old cover, and starts the common morph clock only after
  supported visible fields and their material arrays are ready. Merge requests
  include the eventual coarse fields as well as the fine morph endpoints.
  The renderer lease expires on inactive-view teardown/shutdown, so headless
  streaming cannot wait forever for a nonexistent renderer. Multiple camera
  views contribute to one presentation readiness result. Permanent unsupported
  materials/resource failures retain the explicit ordinary-mesh path.
- Preparing the entire continental cover was rejected during qualification:
  it consumed texture handles even for invisible patches and caused a sky
  texture allocation failure. That run is not performance evidence. Requests
  now carry conservative bounds (including both morph endpoints); only visible
  requests enter the GPU caches.
- The Alpine palette used a thin mountainside scan as a randomly rotated,
  freestanding rock at scale 3–9. That placement rule is removed; nearby authored
  outcrops use the package's solid rock prefab. The scan itself is retained for
  deliberate placement. These scene/palette changes do not delete user assets.
- Grass no longer jumps directly from 7,830 triangles to six crossed-card
  triangles at 14 m. A 1,566-triangle authored intermediate mesh covers 14–45 m,
  using the same placement frame and byte-identical source texture maps. The
  far impostor still switches discretely; this is not a foliage crossfade.
- Physics previously resolved each collider hierarchy with repeated linear
  parent-ID searches through the entity list. Streamed terrain amplified that
  work across thousands of surfaces and several fixed ticks per rendered frame.
  `PhysicsWorld3D` now uses the existing tracked `WorldTransformSnapshot3D` once
  per sync pass. Shape signatures, contact state, pose changes, reparenting and
  collider lifetime remain in the physics owner. Shared transform profiler
  scopes/gauges now use the `Scene.` prefix rather than `Renderer3D.`.
- Embedded Play previously ignored `maximum_fixed_steps_per_frame`; standalone
  already applied it. Play now observes the same cap, drops excess whole ticks
  and retains the fractional remainder. Paused single-step still advances one
  tick. The Profiler shows tick count and dropped time alongside CPU/GPU timing.
  A 50 ms rendered frame at a 60 Hz simulation rate normally needs three ticks;
  the optimization reduces each tick's cost rather than changing that rate.

## Qualification

The repeatable maximized editor run uses `demi-editor --play --project
examples/terrain_3d --profile-frames <csv> --max-seconds 85`. Both captures use
Linux Release, the same source scene and a 5120 × 2742 native window. Performance
samples use the last 500 complete frames after streaming settles. The baseline
already includes the new terrain preparation and intermediate grass; the CPU
comparison isolates the physics snapshot and embedded catch-up changes.

Last 500 complete frames, median / p95 milliseconds:

| Scope | Before | After |
| --- | ---: | ---: |
| Game CPU (update + submission) | 46.692 / 58.665 | 23.585 / 25.462 |
| Editor frame | 49.041 / 61.510 | 25.097 / 27.011 |
| Update | 27.456 / 38.318 | 6.568 / 8.150 |
| Render submission | 19.389 / 22.492 | 16.940 / 18.040 |
| Physics body synchronization per rendered frame | 19.286 / 26.448 | 1.281 / 1.909 |
| Physics body synchronization per fixed tick | 6.426 / 7.059 | 0.872 / 1.214 |
| Game GPU | 11.671 / 11.840 | 12.596 / 14.711 |

Baseline fixed-tick counts: 433 frames with three, 37 with four, 26 with two,
three with five and one with six. After: 265 frames with two and 235 with one.
The simulation timestep was unchanged. GPU time did not improve; this addresses
CPU work and its repeated catch-up cost. Raw captures:
`/tmp/demi-stability-play.csv` (frames 3240–3739) and
`/tmp/demi-stability-play-after.csv` (4379–4878), with matching PNG captures.
These are single-run measurements, not a hardware-independent performance target.

Focused tests cover delayed render acknowledgment without premature publication
or morph advancement, renderer teardown, mapped-resource prewarming before patch
entities exist, fixed-step capping/fraction retention, parent-only translation,
rotation, scaling and reparenting, dynamic pose ownership and deterministic
physics replay. The render test initially mixed prewarming allocation history
with a later retirement fixture; it now uses a separate renderer/capture owner.
No test assertion was weakened.

The 22-test affected subsystem selection and the separately rebuilt bgfx renderer
capture test all pass. All four visible terrain E2E scenarios pass, including the
scripted traversal, distant teleports/revisits and scene unload. Validation checks
206 files with no diagnostics; `git diff --check` is clean. This is not the full
repository gate or a requalification of the previous headless tour issue.

The native traversal trace recorded fallback terrain draws in 265 of 549 frames
with active regional morphs before, and 12 of 375 after. This counter includes
resource fallback activity, not a perceptual count of flashes. Asynchronous
scheduling changes the number of sampled morph frames; residual fallbacks remain
around abrupt viewpoint changes. Raw evidence: `/tmp/demi-stability-before.csv`
and `/tmp/demi-stability-traversal.csv`; E2E summary is in
`/tmp/demi-stability-traversal.log`.

Remaining limits: material classification and shading normals are not morphed;
vegetation models have no temporal crossfade; newly visible content after abrupt
camera movement can still require preparation. This pass does not qualify
Unreal-equivalent rendering, all-world residency, or cooked regional programs.
