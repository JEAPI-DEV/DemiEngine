# Multi-skin and rigid-part GPU palettes

2026-09-15. Milestone 2 compatibility/performance slice; animation LOD and visual
update budgets are not implemented by this change.

## What changed

The original GPU path required one skin and weighted vertices throughout the
model. `GpuSkinPaletteLayout` now gives each referenced `(skin, joint)` pair a
stable matrix row. It also maps rigid owner nodes and a shared identity row.
`GpuSkinnedMesh3D` owns this layout alongside its immutable vertex/index buffers;
the worker-safe `packGpuSkinPalette` resolves it against the existing CPU pose.
The renderer no longer assumes the first skin supplies the whole palette.

- Different skins can use the same joint slot number without sharing a binding.
  Each still uses its own inverse-bind matrix.
- Rigid vertices follow the full owner-node hierarchy, without inverse binds.
  Models animated solely through nodes can also use this path.
  This concerns parts inside the imported model, not a new gameplay bone-socket API.
- Zero-weight skinned vertices retain their local position, matching the CPU
  reference; they are not attached to joint zero or their mesh owner. The usual
  import and entity transforms still apply.
- The shader's unchanged 128-matrix limit applies to the combined referenced
  rows, not declared joints per skin. Unused joints/skins cost no palette rows.
  This is reference pruning, not pose sharing or geometry/animation LOD.
- Invalid references, nonfinite matrices and oversized palettes are rejected;
  unsupported models retain the CPU path. GPU upload validates all encoded
  palette indices, including zero-weight operands. Failed packing clears output.

No authored schema, Lua contract or new asset type is introduced. Shader code is
unchanged. Custom materials, active blends/layers and procedural bone overrides
still use the existing CPU fallback, and the existing normal requirements remain.

## Repeatable probe

`--rig-layout split` creates a temporary two-skin variant of UAL1 Standard by
splitting its two mesh primitives between two mesh owners/skins. Binary vertex,
index, texture and animation buffers are preserved byte-for-byte. It references
52 joint bindings in one skin and 50 in the other, fitting 102 palette rows even
though both declare 65 joints. The source model remains untouched.

The runner uses `demi asset reimport` for the generated manifest and validates
the copied project before runtime launch. Python tests verify buffer preservation,
unchanged animation/accessor metadata, source preservation and refusing to
overwrite an existing output. Initial fixture attempts with incorrect/stale
hashes were rejected before runtime and are not performance evidence.

```sh
SDL_VIDEO_DRIVER=x11 python3 scripts/benchmark_3d_visible.py \
  --binary build/linux-release/demi --output build/multi-skin-repeat \
  --counts 64 1000 --workloads animated --rig-layout split --skinning gpu \
  --vsync off --seconds 8 --warmup-seconds 2 --width 1280 --height 720
```

Use a fresh output directory. `--skinning gpu` requires every character to report
the GPU path; the usual complete-visibility/playback checks remain active. Use
`--skinning cpu` for the current CPU reference, or omit `--rig-layout split` for
the original one-skin regression workload.

## Measurements

Linux Release, Ryzen 7 6800H / NVIDIA RTX 3070 Ti Laptop, Vulkan/X11, 1280×720,
VSync off. One eight-second capture per case, first two seconds excluded. Runs
and builds were sequential. Frequencies, thermals and background activity were
not controlled; these are short-run measurements, not a sustained qualification.

| Two-skin characters | Before: frame p95 | After: frame p95 | After: frame p99 |
|---:|---:|---:|---:|
| 64 | 15.73 ms (CPU fallback) | 0.78 ms (GPU) | 0.89 ms |
| 1,000 | Not measured | 6.92 ms (GPU) | 7.22 ms |

All captures passed resolution, visible-population and playback checks with zero
measured-window discarded fixed time. The before capture reports 64 CPU/zero GPU
characters; the after captures report the full GPU population and no CPU vertex
rebuilds. Reports/logs/binary hashes are retained in
`build/multi-skin-cpu-before/` and `build/multi-skin-gpu-after/`.

These measurements concern the two-skin visual-animation fixture. They do not
qualify character AI/physics, all rig types, Android execution or the whole of
Milestone 2.

Eight focused Debug CTests and four Release CTests passed, including GPU palette
equivalence, the original renderer/skin reference, app-host and animation checks
as applicable. The Python wrapper includes 11 benchmark/fixture checks. Tests
cover distinct inverse binds, rigid-only animation, zero weights, deterministic
row mapping, bad pose/reference rejection and the combined 128/129-row boundary.
Desktop E2E also passed repeated scene entry/exit and cleanup. This is not a full
repository test-suite result.

The frozen 16-character two-skin capture at `build/multi-skin-review.png` was
visually inspected and matched the retained original one-skin capture
`build/gpu-skinning-review.png` pixel-for-pixel. That is a single pose/camera
comparison, not a proof for every asset. The review run is excluded from the
performance comparison above.
