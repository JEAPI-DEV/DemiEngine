# Terrain Milestone 6 qualification

## Implemented boundaries

Palette format 3 uses independent physical candidate lattices for named rules.
Terrain sample resolution no longer determines vegetation density or identity.
Density/category/art controls thin existing sites; minimum spacing compares
immutable neighboring candidates, so a local exclusion cannot cause distant
replacement chains. Biome, slope, moisture, water distance, protection and
exclusion checks use the shared terrain constraints. Final graph overlays
refilter retained candidates and reproject their heights without regenerating
the graph. Creating candidates that upstream graph rules rejected still requires
changing that upstream source and generating again.

Scene terrain placements own `scatter_exclusions`. The Inspector's **Convert to Scene Object**
command (called promotion internally) adds an ordinary authored object and suppresses its generated source in
one validated history command. Undo/Redo and save/reload preserve both sides.
This deliberately does not modify the shared terrain asset. Promoted prefabs
retain their authored materials, real model LODs and effective collision policy.

Collision-free visual prefabs and direct models use `MeshInstances3D` batches.
Gameplay prefabs remain real entities; collidable prefabs retain their authored
shapes. Direct model rules must explicitly choose no collision, because a model
reference does not describe a gameplay collider. Ordinary Materials support
bounded wind in color, shadow and depth passes. Normals are not wind-deformed;
collision stays fixed. Frustum bounds include model LOD variants and wind.

Package prefabs resolve by declared files and stable document IDs. Nested
package sources can reference their own package and declared direct
dependencies. Source and relocated cooked package resolution are tested.
Generated prefab scenery participates in implicit scene resource discovery,
including LOD models and Materials. Entity IDs and object keys containing URI
text are not asset references.

## Executable probes

`packages/sources/demi.terrain.vegetation` contains original CC0 meshes,
Materials, prefabs and a woodland palette. Eighteen indexed glTF models provide
three genuinely different detail levels for six shapes. Source checks cover
finite geometry, valid indices, nondegenerate triangles, normalized normals,
decreasing triangle counts and synchronized crown/trunk culling.

`examples/vegetation_3d` exercises a contained lake, shoreline reeds, grass,
trees, rocks, slope filtering and road/building exclusions. Its default scene
produces 99 renderable entities rather than one entity per grass blade.

## Qualification environment

Linux, Release (`-O3`), SDL3/X11, bgfx/Vulkan, 3840 × 2022, 4× MSAA.
AMD Ryzen 7 6800H; NVIDIA RTX 3070 Ti Laptop GPU, driver 615.71.09.
Only one engine process runs during captures; compilation and test runs are
stopped. Native measurements exclude startup frames and require focus.

The first native run exposed missing generated-prefab LOD residency. A second
run exposed substring-based resource discovery misreading palette URIs inside
generated entity IDs. Both received shared fixes and regression tests.

The first successful overview had median CPU render submission 47.382 ms,
visibility extraction 7.054 ms and combined directional shadow work 28.342 ms,
despite only 75 visible meshes out of 9,976 considered. GPU median was 3.713 ms.
These are pre-optimization measurements, not the final acceptance result.
They motivated early viewer-distance rejection, cached camera/owner math and
one conservative transformed bounding sphere instead of eight transformed
corners per copy and pass. No density reduction was used to address this cost.

## Native results

Each row uses frames 100–299 of a 300-frame capture. Values are milliseconds;
render submission includes presentation pacing, so it is not pure CPU work.

| View / implementation | Render median / p95 | GPU median / p95 | Main visibility median |
| --- | ---: | ---: | ---: |
| Overview before fixes | 47.382 / 52.690 | 3.713 / 10.924 | 7.054 |
| Overview final | 6.444 / 6.976 | 3.149 / 3.423 | 0.089 |
| Close view before preparation caches | 19.107 / 21.480 | 4.026 / 4.097 | 1.037 |
| Close view final | 8.907 / 9.977 | 4.884 / 5.197 | 0.228 |
| Same close view, grass category disabled | 6.490 / 7.072 | 4.906 / 5.238 | 0.058 |

The close view uses camera position `[55,9,73]`, target offset `[-14,-4,-32]`.
It retains 1,302 visible meshes, 36 batches and 32,002 reported triangles before
and after preparation caching. Both populated views consider 9,976 meshes;
the terrain Inspector reports 9,937 procedural placements. The final overview
has 77 visible meshes and 64 batches, two more crowns than the initial capture
because the package's crown/trunk culling distances were corrected. Density
and geometry detail were not reduced to produce these CPU improvements.

GPU samples come from a dynamically clocked laptop. They are observed costs,
not a claim that the CPU changes improve GPU execution time. The grass-disabled
comparison also disables shoreline reeds because both use the grass category.

Source and cooked native captures use the same scenery and model tiers. Both
start with 99 renderable entities. The package contains no physics body per
blade; the populated scene reports 75 physics bodies including terrain and
authored objects. Cooked project validation and three-frame headless startup
pass, including a separately saved promoted-tree scene.

The editor was inspected at its actual 5120 × 2806 desktop window size. Native
interaction verified grass promotion, Undo restoring all 9,937 placements,
selection of a generated tree crown, promotion of its owning tree, and Save.
The saved source contains one suppression key and one nested authored wrapper
with the original prefab and collision override. That saved project validates
and cooks. The previously recorded nested-parent Inspector display issue in
`tofix.md` remains; source hierarchy ownership is correct.

Presentation caches hold source preparation, copy transforms, conservative
bounds and final model matrices only during one presentation. They reset before
the next frame; per-view frustum and viewer-distance decisions remain separate.
Tests cover distinct worlds, owner movement, same-size instance mutations,
matrix equivalence, lazy distant copies and camera/shadow reuse. Surface
preparation is cached per source and selected model tier within a pass.

Permanent profiler scopes now distinguish extraction, terrain preparation,
shadow-section compaction, surface ordering and mesh submission. The wind
clock advances before shadow rendering so all passes receive the same phase.

Reproduce a native capture with:

```sh
./build/linux-release/demi run --project examples/vegetation_3d \
  --window-size 3840x2022 --max-frames 300 --profile-frames /tmp/vegetation.csv
```

Capture evidence from this run is named `/tmp/m6-overview-final.*`,
`/tmp/m6-close-matrices.*`, `/tmp/m6-close-no-grass.*` and
`/tmp/m6-woodland-cooked.*`. Native promotion screenshots and saved JSON are
`/tmp/m6-promoted-result.png`, `/tmp/m6-undo.png`,
`/tmp/m6-tree-promote4.png` and `/tmp/m6-promoted-scene.json`.

## Remaining scope

This qualifies lightweight vegetation, not large-world streaming or high-end
foliage rendering. Instance iteration remains linear; spatial residency and
cross-system streaming belong to Milestone 7. Wind uses simple bounded bending
with unchanged shading normals and collision. The original low-poly package
is functional starter art, not a photorealistic asset set.

## Validation and publication

Focused Debug tests pass for palettes, scatter rules/runtime/layout/constraints,
local graph replay, component schemas, materials, scene documents, package
prefab resolution, runtime assets, validation, visibility and native rendering.
Additional regressions cover changing a colliding placement's prefab source
without changing its ID, retiring obsolete children, failed replacement rollback,
and switching static collision to a render batch and back without duplicate
objects or retained colliders.

The broader terrain gate caught repeated flat-surface LOD error work and an
outdated cache fixture. The exact-flat shortcut reduced the unchanged Debug
streaming test from roughly 39 seconds to 0.71 seconds. The cache test passes;
the large terrain graph test passes in 17.77 seconds and its Debug example
smoke passes in 27.53 seconds under the existing 30-second limit. This is a
focused terrain/editor/render qualification, not a claim that the entire
repository or Android/Windows was qualified.

Vegetation, Rocky Valley, terrain, terrain graph and water interaction examples
all validate with no diagnostics. Original vegetation source geometry checks
pass. The converted-tree scene validates, cooks, and starts in both Debug and
Release headless runs after the shared JSON reference scanner fix.

Published `demi.terrain.vegetation` 1.0.0 and migrated
`polyhaven.materials.terrain` 1.2.0. Store tests, README/catalog replacement
checks, 106 Twig templates and production cache warmup pass; online package
pages and terrain documentation were checked after deployment.
