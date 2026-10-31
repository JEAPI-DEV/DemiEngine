# Regional terrain startup, vegetation range and performance

This records the range/startup pass before geometry morphing. The subsequent
[morphing and performance qualification](regional-terrain-morphing.md) updates
the transition behavior and frame-time results; the measurements below retain
their original scope.

## Research and implementation boundaries

Primary references consulted on 2026-10-31:

- [NVIDIA, GPU-based geometry clipmaps](https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-2-terrain-rendering-using-gpu-based-geometry): nested regular grids, retained coverage and transition-region morphing address terrain complexity and visual popping. Demi retains its existing balanced quadtree; it does not claim to implement clipmaps or continuous geomorphing.
- [Epic, Foliage Mode](https://dev.epicgames.com/documentation/en-us/unreal-engine/foliage-mode-in-unreal-engine): static foliage uses hardware instancing and clustered visibility; distance fading reduces abrupt culling. Ordinary independently simulated actors have a different cost. Demi uses native instance batches for collision-free visual foliage.
- [Unity, terrain demo](https://unity.com/blog/games/experience-the-new-unity-terrain-demo-scenes-for-hdrp-and-urp): GPU instancing and billboard representations are practical vegetation tools. The new distant tree is an atlas-backed impostor, with mesh tiers retained nearby.
- [Guerrilla, GPU-based procedural placement](https://www.guerrilla-games.com/read/gpu-based-procedural-placement-in-horizon-zero-dawn): describes generating environments around the player. Demi's current placement preparation remains CPU-worker based; GPU-generated placement is future work.

These references inform mechanisms, not claims of equivalent production scope.

## What changed

`RegionalTerrainCover` plans the opening cover on a worker before preparing its
meshes. Planning uses exact height extrema on each canonical unstitched patch
lattice, without computing unused normals/materials. The patch budget comes from
the region's payload budget with transition headroom. The initial cover remains
balanced and complete; cancellation/retirement use the existing residency owner.
This avoids constructing and publishing every intermediate coarse mesh. Ordinary
camera-driven refinement retains its complete previous cover until replacements
are ready. Edge stitching now looks up adjacent quadtree cells/ancestors instead
of comparing every patch with every other patch.

A native, non-serialized presentation marker keeps the opening landscape behind
a preparation view until the requested terrain cover settles. The standalone
runtime and embedded/authoring views display a preparation status. Later camera
movement does not hide the world again. This does not yet morph moving-camera
LOD transitions or pause gameplay simulation while terrain is preparing.

Vegetation can use independent ground-cover and canopy layers, with stable cell
identities including the layer. Grass/props stay in the near layer; trees/rocks
use the canopy when enabled. Both retain canonical sampling and share one
vegetation residency budget. Near vegetation has startup priority; distant canopy
work starts after initial terrain preparation. Revisit/retirement does not let
one layer evict cells belonging to the other.

The Alpine example now requests grass within 768 m and draws it to 700 m,
compared with its old 180 m draw distance. Tree/rock residency reaches 3,072 m,
compared with the old shared 512 m radius. Grass clustering is less aggressive.
The new tree prefab uses the same CC0 source art, combined near/medium meshes,
and a six-triangle distant atlas model; the previous reused medium tier had
6,000 triangles across three parts. `scripts/art/prepare_woodland_tree.py` runs in
a separate Blender process and produces source glTF for the normal importer.
The original package is unchanged. The distant representation is static, and
the new imported tree materials do not reproduce the old package's custom wind.

Renderer-owned compiled opaque instance plans now serve ordinary camera and
shadow views, including the shadow LOD distance floor. Eligible source batches
retain their raw visible copies and emit one representative dispatch into the
ordinary draw list. Material sections are resolved per selected model tier,
rather than expanded/copied for every blade/tree. Transparent/custom/animated
sources retain their existing path. Per-presentation reset and per-cascade
scratch preserve invalidation and view isolation.

`EditorAssetIndex` now indexes manifest/source paths to snapshot-local indices.
The browser rejects entries outside the visible folder before metadata lookup.
This removes repeated full asset scans/path copies during drawing. Copies and
refreshes of the index do not retain pointers into another snapshot.

## Measurements

Linux Release, Vulkan, NVIDIA RTX 3070 Ti Laptop GPU, driver 615.71.09. Final
runs used the user's maximized primary-display window: **5120 x 2742**, with a
**2732 x 1710** terrain viewport. The original smaller-window exploratory runs
are not used as a full-frame baseline. No concurrent builds/tests ran during the
reported performance captures.

Both maximized captures used the expanded vegetation, impostor tree and indexed
asset browser. The later capture additionally uses compact representative
instance dispatch and direct initial cover planning. Each lasted 100 seconds;
steady figures below use the final 600 frames of each single run.

| Measurement | Earlier maximized capture | Final capture |
| --- | ---: | ---: |
| Initial terrain reveal, from profiled frame-loop start | 43.080 s | 21.713 s |
| Frame interval median / p95 | 34.946 / 36.335 ms | 31.127 / 32.165 ms |
| Editor CPU frame median / p95 | 34.487 / 35.816 ms | 30.705 / 31.635 ms |
| Viewport CPU median | 32.465 ms | 28.418 ms |
| Visibility extraction median | 6.284 ms | 4.447 ms |
| Directional shadow CPU median | 11.079 ms | 10.892 ms |
| GPU median / p95 | 11.859 / 12.189 ms | 11.961 / 12.283 ms |
| Assets panel median | 0.273 ms | 0.279 ms |

The earlier diagnostic capture before indexing measured roughly 15 ms in the
Assets panel; its window layout changed during the run, so it is evidence of the
lookup bottleneck, not a comparable total-frame benchmark.

Final terrain: 1,057 active patches, 1.831055 m finest spacing, 83 vegetation
cells, zero pending jobs and zero terrain budget misses. Accounted payload:
513,007,616 terrain bytes and 132,306,620 vegetation bytes. These exclude live
world copies, GPU resources, shared art and allocator overhead. The opening
terrain appeared with 58 vegetation cells; the farther canopy settled by about
35.5 seconds. Initial resource preparation before the profiled frame loop is not
included in the reveal measurement. Streaming/publication and resource upload
can still cause larger transient stalls than the steady-state table.

Raw local artifacts: `/tmp/demi-range-maximized.csv`,
`/tmp/demi-range-qualified.csv`, `/tmp/demi-range-qualified.png`,
`/tmp/demi-range-measurements.json`. Temporary captures are not shipping assets.

The measured frame rate remains around 32 FPS at this viewport size, with CPU
submission/shadow work still significant. This is not a 60 FPS qualification,
miles of individually rendered grass, continuous terrain geomorphing, or a
production-scale open-world claim. Shader/foliage appearance and seamless
moving-camera transitions remain further work.

## Verification

- Rebuilt relevant Release targets; all **79 selected CTest checks passed**.
- New regressions cover canonical height extrema, bounded initial coverage,
  layered vegetation ownership, preparation visibility, compact batch dispatch,
  and asset-index copying/refresh. Existing renderer tests exercise materials,
  LODs, shadow views, resource lifetime and mutation fallback.
- The visible desktop E2E runner passed all **four** terrain-example scenarios,
  including regional teleport/revisit, finite tour playback and highland unload.
- Project validation checked **203 files with no diagnostics**.
- Blender produced the near/mid/distant glTF sources; native asset import and
  reimport updated audited manifests/generated output. The atlas and maximized
  editor captures were visually inspected.
- The previous headless tour-transition failure is not newly qualified by these
  visible tests. `git diff --check` is clean; unrelated worktree changes remain.

```sh
cmake --build --preset linux-release --target demi-editor demi
ctest --test-dir build/linux-release --output-on-failure \
  -R 'terrain|editor.*hierarchy|editor-scene-view-state|editor-asset-workflow|bgfx-renderer3d|scene-visibility3d|bgfx-gpu-resources|bgfx-graphics-device'
./build/linux-release/demi validate examples/terrain_3d
./build/linux-release/demi test linux --project examples/terrain_3d --timeout 300
./build/linux-release/demi-editor --project examples/terrain_3d \
  --profile-frames /tmp/terrain-frames.csv --max-seconds 100
```

For comparable measurements, maximize the editor on the same primary display,
use the default dock layout/opening camera and record actual viewport dimensions
from `Renderer3D.viewport_width`/`Renderer3D.viewport_height` gauges.
