# Regional editor usability qualification

## Scope (2026-10-31)

The existing camera-driven regional implementation remains the shared owner of
terrain and vegetation residency. The editor now enables that vegetation path,
passes its private preview prefab service and stays awake through pending worker
jobs and queued cell publication. Cells redirect picking/hierarchy visibility to
the authored terrain; they do not become authored scene entities. Graph edits
resolve the actual scatter palette instead of replacing it with the deliberately
empty finite-overview palette.

Regional scenes with an enabled authored camera start there. Whole-scene framing
remains available explicitly. RMB + wheel adjusts transient editor fly speed
from 0.1 to 10,000 m/s; Shift multiplies it by four. Ordinary wheel zoom retains
its existing meaning. The Alpine example's initial camera now agrees with its
flight script, which supports mouse/pitch look, correct screen-right strafing
and turning, normalized combined movement and cursor release on destruction.

Terrain array textures request anisotropic filtering. Explicit per-projection
gradients preserve the footprint across divergent biome/projection branches;
mirrored layers reverse those gradients. Per-layer clamp retains guarded mip
sampling because the physical shared sampler repeats. The backend enables the
hardware anisotropy ceiling on initialization and reset; only opted-in textures
use it. GLSL, GLES and SPIR-V shader variants compile in the native build.

## Measurement

Linux Release, Vulkan, NVIDIA RTX 3070 Ti Laptop GPU (driver 615.71.09),
fixed Alpine opening camera, 1600 x 1000 editor window,
isolated XDG state, 75-second capture. Window size includes dock panels; it is
not a 1600 x 1000 terrain render target. No concurrent test workload was active
during the reported final 1,406 frames (frame 1500 onward).

| Scope | Median | p95 | Maximum |
| --- | ---: | ---: | ---: |
| Frame interval | 35.08 ms | 40.41 ms | 51.18 ms |
| Editor CPU frame | 34.69 ms | 40.02 ms | 50.32 ms |
| Editor UI | 11.34 ms | 12.79 ms | 18.34 ms |
| Viewport submission | 21.84 ms | 24.01 ms | 32.37 ms |
| Directional shadow submission | 11.94 ms | 13.27 ms | 18.31 ms |
| GPU | 9.23 ms | 10.05 ms | 10.75 ms |
| Regional update | 0.80 ms | 5.01 ms | 7.23 ms |

The final cover retained 1,051 patches at finest spacing 1.831055 m, 22 vegetation
cells and no pending terrain jobs. This is current-cost evidence, not a measured
speedup. The earlier 5K capture framed the entire region with vegetation disabled
and cannot serve as a comparable performance baseline. Initialization/refinement
has larger spikes (527 ms observed after frame 1000); the steady-state table does
not describe startup latency. CPU shadow submission and UI still need work for a
60 Hz target. This does not qualify full-world gameplay or solve the remaining
landscape art, vegetation shading and distant terrain quality limitations.

Raw local evidence: `/tmp/demi-regional-final.csv`,
`/tmp/demi-regional-final.png`, `/tmp/demi-regional-final.log`.
These temporary artifacts are not repository assets.

## Verification

The native Release desktop E2E runner passed all four terrain-example scenarios:
regional teleport/revisit and unload, finite tour playback, chapter seeking, and
highlands traversal/unload. `demi validate examples/terrain_3d` checked 199 files
with no diagnostics. Focused editor tests cover camera startup, fly speed without
zoom, preview vegetation completion, hierarchy/picking ownership and unchanged
authored documents. An isolated Lua check exercised strafe/turn direction,
diagonal normalization, pitch and mouse-capture release.

The headless example E2E run remains unsuccessful: its first test timed out
waiting for `scene://terrain_3d/main` while still in Alpine, then its fixed frame
budget expired during the second test. The visible native suite passes. Do not
report a universal green gate or headless traversal qualification from this run.

The broader terrain gate exposed an outdated pre-existing asset-validation
fixture: it loaded only project-authored manifests even though the example's
materials now reference package textures. It now uses the normal package-aware
registry and retains the stricter optional missing-map checks for scalar surfaces.

After rebuilding all 74 relevant executable targets, 75 of 76 selected CTest
checks passed. The remaining asset-validation fixture passed after the
package-aware assertion correction above. All 76 selected checks therefore pass
with the final sources; this is not a full repository test run. `git diff --check`
is clean. Source validation and visible E2E evidence are recorded separately from
the unsuccessful headless E2E attempt.

Reproduce the focused gate with:

```sh
ctest --test-dir build/linux-release --output-on-failure \
  -R 'terrain|editor.*hierarchy|editor-scene-view-state|bgfx-gpu-resources|bgfx-graphics-device'
./build/linux-release/demi test linux --project examples/terrain_3d
python3 scripts/capture_editor_window.py --project examples/terrain_3d \
  --width 1600 --height 1000 --settle-seconds 50 --output /tmp/regional-editor.png
```
