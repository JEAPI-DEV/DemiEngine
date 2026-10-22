# Silvermere showcase qualification

Current checkpoint, 2026-10-22. Silvermere is a repeatable lightweight-3D
landscape tour, not feature parity with the supplied Flax screenshots.

## Current workload and behavior

The native 768 × 768 m terrain graph provides ridged highlands, meadow/basin
masks, lake carving, biomes and deterministic scatter. Its prepared output has
149,125 untruncated placements: 120,825 grass, 4,525 reeds, 16,426 sorrel,
3,488 flower patches, 2,820 shrubs, 985 trees and 56 rocks. These are batched
visual placements, not independently simulated entities or simultaneous visible
counts. Scatter collision is disabled for the camera showcase.

The seven-chapter, 70-second looping tour uses reusable `demi.camera.paths`
Lua playback with HUD pause/resume, restart and chapter seek. The loading scene
reports preparation before activation. Native standalone startup now applies
authored fullscreen settings before graphics initialization and updates the
scene title on activation; 3840 × 2160 fullscreen was verified.

Woodland 1.2.0 supplies credited CC0 textured trees, original grass patches,
shrubs, sorrel and Gazania. Grass selects 6,422 triangles nearby, 4,626 after
12 m and a ten-triangle baked coverage model after 38 m. The distant model has
four side views and a top view of the actual patch; no extra grass cull cutoff
applies. Trees and rocks also have no distance cutoff before the 1800 m camera
far plane. Reeds retain 90 m; shrubs and flowering cover retain 220 m.
Only the `showcase_grass` wrapper disables `MeshRenderer.casts_shadows`.
Trees, reeds, sorrel, flowers, shrubs and rocks retain default casting. Grass
still receives shadows and contributes to camera/water depth. Shadows cover
180 m independently of main visibility.

The CC0 3TD/Ron Kapaun ruined cathedral uses six material sections. Shared glTF
import/rendering now preserves per-section materials, maps, samplers and static/
skinned index ranges; native visual inspection confirmed the corrected church.
Ground uses local meadow/rock materials over the shared Poly Haven resources;
the sky is the credited Kloppenheim texture also used in Amber Pass.

## Final native measurement

Linux Release, Vulkan, one standalone process, 3840 × 2160, authored 4x MSAA.
`/tmp/silvermere-shadow-final.csv` records 6,500 frames. Analysis includes only
the activated landscape and excludes its first 180 frames (3,925 warm samples).
Median/p95 frame work is **21.39/27.43 ms**, CPU render preparation
**18.03/25.10 ms**, and GPU **18.57/27.88 ms**. MRT capture reason is zero
throughout: opaque color and analytic depth share geometry submission, with
nearest-valid-sample depth resolve. No native validation errors were observed.
See [runtime qualification](runtime-efficiency-qualification.md) for capability
fallbacks and detailed matched-workload evidence.

A main-geometry-matched bin against `/tmp/silvermere-mrt-native.csv` has
53,118→53,120 visible records and 8.058→8.056 million main triangles (131/249
samples). Shadow triangles fall 45.94→2.39 million; frame work 36.82→19.04 ms,
CPU preparation 31.42→14.70 ms and GPU 37.88→19.85 ms. This comparison combines
the explicitly requested grass no-cast policy, sampling-aware static shadow LOD
and conservative receiver-hull culling. It does not isolate their contributions,
and both captures already use MRT. Moving-tour bins are not identical-camera
or frame-number-matched experiments.

Other casters remain: the final far cascade has median 2.20 million triangles,
10 batches and 3,267 low-LOD casters. Dense matched views near 7.44 million main
triangles still cost 26.86 ms GPU versus 26.30 ms previously. Native 4K60 is
**not achieved**; main rendering is now the dominant remaining GPU workload.

## Checks and remaining work

Camera package tests and native HUD/camera E2E cover sampling, loop/clamp,
movement, pause, restart, chapter poses and final-to-first wrap. Native resource,
shader, visibility, shadow layout/hull, opaque-plan, component-schema and runtime
object-model regressions cover the shared renderer/component changes. Final
repository-wide release validation is recorded by the checkpoint owner separately;
these results are not a claim that every platform or full suite is green.

The updated route's sampled minimum terrain/water clearance is 4.91 m. A
conservative tree-envelope check flags a close canopy passage on leg two; the
route is not certified continuously collision-free.

Outstanding acceptance work:

- Smooth material-aware LOD fades and coverage/silhouette checks across all
  chapters; baked grass coverage does not supply seamless transitions.
- Stronger art composition, varied woodland/outcrops and close landmark views.
- Sustained native 4K60 with comparable main geometry and materials; retain
  CPU/GPU scope evidence instead of substituting placement counts for quality.
- Automatic imported-model LOD generation is absent. Existing model/terrain LOD
  selection uses authored assets. Evaluate meshoptimizer at import/cook with
  UV/material seam and foliage coverage checks.
- Native scene-animation timeline, volumetric lighting, GPU lightmap baking and
  the other requested editor/rendering features remain separately scoped in the
  [3D showcase plan](../3d-showcase-plan.md).

## Historical evidence

Early woodland builds used finite grass/tree cutoffs, 134,167 placements and
then a 126,391-placement composition revision. Those counts and the old 220 m
grass limit are superseded by the current workload above. Initial fullscreen
claims actually described a maximized 3840 × 2022 window; the native fix replaced
that behavior. The initial art-only pass needed no new native code, but this
checkpoint includes shared startup, glTF, prefab-resolution and renderer work.

Earlier moving-tour traces (`silvermere-lush-active`, `silvermere-order-fixed`,
`silvermere-gather-fixed` under `/tmp`) established expensive opaque sorting and
repeated instance gathering. Matched dense frame work improved 135.8→48.7→44.4 ms
through those stages without reducing comparable main triangles. These historical
captures are not current budgets. Shared prefab dependency/resolution work later
reduced measured preparation to about 288 ms and publication to about 1.08 s;
loading HUD availability does not eliminate all startup work.

Earlier E2E/validation evidence includes `/tmp/silvermere-final-e2e.log`,
`/tmp/silvermere-validate.log`, `/tmp/silvermere-fullscreen-e2e.log` and
`/tmp/silvermere-final-native-tests.log`. The original 140-source validation count
predates the cathedral and is not the final source count. Publication status is
owned by the release checkpoint, not inferred from these local test artifacts.
