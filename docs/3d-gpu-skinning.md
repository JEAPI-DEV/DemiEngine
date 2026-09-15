# Vulkan GPU skinning — Milestone 2

2026-09-14. GPU skinning is now the default optimization for supported models on
Vulkan. No new model format, component, prefab or package is required.
The later [Milestone 2 qualification](3d-milestone-2-qualification.md) includes
final desktop and physical Android measurements with the completed optimizations.

## Implementation boundary

The glTF loader preserves authored vertex normals and exposes an engine-owned
`Pose` containing node and skin matrices. `samplePose` and `bindPose` evaluate
that pose without touching vertices; the existing CPU `samplePositions` and
`bindPosePositions` call the same evaluator and then deform positions.

`GpuSkinnedMesh3D` owns immutable model vertex/index buffers containing original
positions, normals, colors, UVs, joints and weights. Each entity owns a cached
bone palette, not a private deformed GPU vertex buffer. Pose sampling still uses
the CPU worker pool; Vulkan's vertex shader applies skinning, import conversion,
and the entity transform. Shader resources, uniform updates and draw submission
remain render-thread-owned. No CUDA, PhysX or vendor-specific dependency is added.

The shader preserves the CPU position convention for non-unit weight sums:
weighted joint-space xyz is accumulated before applying import translation.
Normals use the inverse-transpose of the blended skin/import/model transform,
including nonuniform scale and reflections, with a defined fallback for singular
normal directions. Authored smooth/hard normals replace the CPU fallback's
per-pose geometric reconstruction, so lighting is not pixel-identical between
the two paths. No polygons or animation updates are removed.

The vertex shader skips palette loads/arithmetic for zero-valued trailing
influences. It does not drop small weights, renormalize them, or change the
four-influence format. The UAL probe has 3,734 one-influence, 3,746 two-influence,
466 three-influence and 600 four-influence vertices. This is useful for sparse
weights, not a promise of the same speedup on every rig.

GPU-only preparation batches contain up to 128 palettes (at most 1 MiB of palette
output). CPU or mixed batches retain the previous 16-job limit and vertex-output
budget. Oversized CPU geometry is handled alone. Resource caches are invalidated
on model reload, source changes and CPU/GPU path changes. Frozen poses remain
cached; uniforms are still bound for each draw.

## Supported subset and fallback

- Vulkan (including the standard Android Vulkan renderer); Noop is supported for
  pipeline tests only. OpenGL/OpenGLES keep CPU skinning in this initial slice.
- Multiple skins and animated rigid parts, with at most 128 referenced matrix
  entries combined and the importer's four-influence vertex format. Indexed
  triangles and vertices with finite
  weights and valid authored normals. Invalid inactive joint indices are replaced
  with zero before upload, so even zero-weight shader operands stay in bounds.
  The palette maps referenced `(skin, joint)` pairs, rigid owner nodes and an
  identity entry where needed. Unused joints do not consume entries. See the
  [multi-skin follow-up](3d-multi-skin.md) for the expanded contract and evidence.
- Built-in material, no procedural bone segments, no active layer/blend. Other
  cases retain the existing CPU behavior; this does not add missing blend/layer
  functionality to either path.

Use `DEMI_GPU_SKINNING=0` before launch to force CPU skinning. This is a diagnostic
override, not an authored project setting. Unsupported content falls back; actual
resource/palette errors are reported, not disguised as successful GPU rendering.

## Benchmark workflow

```sh
SDL_VIDEO_DRIVER=x11 python3 scripts/benchmark_3d_visible.py \
  --binary build/linux-release/demi --output build/gpu-skinning-repeat \
  --counts 64 128 1000 2000 --workloads animated --skinning gpu \
  --vsync off --seconds 12 --warmup-seconds 2 --width 1280 --height 720
```

Use a new output directory. Repeat with `--skinning cpu` for the CPU reference.
`--skinning gpu` requires the entire visible crowd to report GPU skinning, and
`cpu` requires CPU skinning; missing telemetry or partial fallback invalidates
the capture. Normal resolution, visibility, playback and timestamp checks remain.
`auto` records the chosen populations without requiring a particular path.

The UAL1 model has 8,546 vertices, 13,744 triangles and 65 joints. All crowd
characters keep independent playback state; there is no pose sharing, geometry
LOD, reduced animation update frequency, AI or character physics in this probe.
Counts are therefore visual-animation evidence, not a whole-game entity promise.

## Initial measurements

Ryzen 7 6800H, Linux Release, SDL X11, bgfx Vulkan, 1280×720, VSync off. NVIDIA
RTX 3070 Ti Laptop is vendor/device 0x10de/0x24a0; Radeon 680M is 0x1002/0x1681.
Each build, benchmark and GPU run was sequential. Frequencies, thermals and
background desktop activity were not controlled.

Initial single-run frame-time p95/p99 in milliseconds:

| GPU/path | Characters | p95 | p99 |
|---|---:|---:|---:|
| NVIDIA / CPU reference | 64 | 15.82 | 16.31 |
| NVIDIA / CPU reference | 128 | 39.89 | 41.39 |
| NVIDIA / GPU | 64 | 0.87 | 1.04 |
| NVIDIA / GPU | 128 | 1.25 | 1.40 |
| NVIDIA / GPU | 256 | 2.13 | 2.48 |
| NVIDIA / GPU | 1,000 | 7.73 | 8.41 |
| NVIDIA / GPU | 2,000 | 16.82 | 18.76 |
| Radeon / GPU | 64 | 1.02 | 1.36 |
| Radeon / GPU | 1,000 | 8.18 | 9.39 |

CPU and small GPU runs lasted eight seconds with two seconds warmup; NVIDIA
256–2,000 runs lasted twelve seconds, and Radeon runs ten seconds, both with two
seconds warmup. All passed capture/path checks with zero measured-window discarded
fixed-step time. Startup clamping/discarding remains in the raw reports.

A subsequent one-minute NVIDIA 2,000-character run, excluding five seconds warmup,
measured 16.14 ms p95 / 18.00 ms p99 and zero measured-window discarded time. These
initial results predate increasing GPU-only palette batches from 16 to 128;
they are retained as development-stage evidence rather than overwritten.

With the final 128-palette GPU-only batches, another one-minute NVIDIA run at
2,000 characters measured **12.98 ms p95 / 14.47 ms p99**, with zero discarded
fixed-step time after the five-second warmup. GPU time was 6.55 ms p95;
preparation wall time was 5.46 ms p95. These overlap with other work and must
not be added as independent percentile budgets. The maximum frame interval was
24.75 ms, so this is not a claim that every frame stayed below 16.67 ms.
The final report is `build/gpu-skinning-final-minute/`.

A final 1920×1080 NVIDIA sweep (twelve seconds, two seconds warmup) measured
6.39/6.85 ms p95/p99 at 1,000 characters and 13.26/14.84 ms at 2,000. Both
captures passed all visibility, GPU-path and resolution checks with zero discarded
fixed time after warmup. This extends the result to 1080p, not to richer game
content. Reports: `build/gpu-skinning-1080/`.

Reports and binary hashes: `build/gpu-skinning-before`, `gpu-skinning-initial`,
`gpu-skinning-scaling`, `gpu-skinning-radeon`, and `gpu-skinning-minute` (all under
`build/`). Radeon selection used the installed Radeon Vulkan ICD via
`VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_icd.json`; vendor/device telemetry
confirmed the actual GPU. Do not treat a requested adapter as proof of selection.

## Validation and remaining limits

Numerical tests compare encoded GPU joint/weight data and sampled palettes with
the CPU position reference across time, loop/clamp policies, non-unit weights
and reflected/nonuniform import conversion. They cover unsupported skin counts,
missing normals, unweighted vertices, invalid joints, frozen caches, reloads,
CPU/GPU switching, and palette-only/mixed batches. Noop tests exercise pipeline
and resource wiring, not actual shader execution.

Vulkan screenshots of the same frozen 16-character scene were inspected on both
paths. All 8,776 character silhouette pixels (orange foreground: red greater than
green and blue) matched; shading differs with authored normals as described above.
Those screenshots are `build/gpu-skinning-review.png` and
`build/gpu-skinning-cpu-review.png`. This is one pose/camera comparison, not a
general GPU readback proof for every rig.

No physical Android device was attached. Android runtime execution, long thermal
soaks, leak/race sanitizer qualification, more rig/material types, animation LOD,
and mixed gameplay remain open. Milestone 2 as a whole is not complete.

Executed validation: eight focused Debug CTests and four Release CTests passed,
including the new GPU-data/pipeline test, CPU renderer reference, glTF skinning,
animation state machine, 3D app host, mesh deformation and benchmark summaries
as applicable. Desktop E2E passed repeated crowd entry/exit and cleanup with both
`DEMI_GPU_SKINNING=1` and `=0`. The animation project validates for Linux/Android,
and the procedural-spider project validates. This is not a full-suite result.
