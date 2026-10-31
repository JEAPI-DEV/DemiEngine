# Terrain and foliage visual-quality qualification

2026-10-31, Linux Release. This pass addresses material and source-art defects;
it does not claim Unreal-equivalent rendering or change the engine's lightweight
3D capability boundary.

## Diagnosis and references

The grass glTF declared `alphaMode: MASK`, but its diffuse PNG was RGB: there was
no alpha channel to discard. About 79% of the atlas was black background. This
made background portions of the grass geometry render as black triangles.
Lighting adjustments alone could not correct that source-data problem.

The original opacity map was retrieved from the official
[Poly Haven asset](https://polyhaven.com/a/grass_medium_01) file API, using the
published 2K PNG URL and verifying MD5 `0810166131307c1094d37e73a8490542`.
Provenance is retained in the grass source's `SOURCES.json`, with the existing
CC0 licence. Blender packed the original opacity into RGBA albedo; the model
retains all 7,830 triangles, with triangle positions matching the previous source
to 1e-6 metres. Export keeps MASK mode instead of accepting Blender's automatic
BLEND conversion, so depth writing and instancing remain available.

The lighting approach is informed by
[Epic's foliage shading model description](https://dev.epicgames.com/documentation/en-us/unreal-engine/shading-models-in-unreal-engine)
and [NVIDIA's Crysis vegetation chapter](https://developer.nvidia.com/gpugems/gpugems3/part-iii/rendering/chapter-16-vegetation-procedural-animation-and-shading-crysis).
Thin leaves benefit from a separate transmitted-light approximation. Demi's new
scalar model is much narrower than those engines' complete rendering systems.

## Shared engine changes

- `MaterialSurface` owns `foliage_transmission` (default 0) and
  `ambient_occlusion_strength` (default 1), both validated in [0,1]. The Material
  editor exposes them; imported materials can retain their texture bindings and
  receive Mesh Renderer `material_properties` overrides.
- Built-in PBR meshes orient enabled foliage normals toward the viewer and add
  an albedo-colored, view-dependent backlighting term. Shadow visibility is
  respected. This is not refraction, opacity, full subsurface transport or GI.
  AO strength affects ambient lighting only. GPU terrain arrays pack AO strength;
  foliage transmission uses the explicit ordinary-mesh fallback on terrain.
- Imported glTF MASK albedo mips filter linear color with alpha weighting, then
  adjust mip alpha coverage against the authored cutoff/base-alpha factor.
  The unchanged base image and ordinary color/data/normal/BLEND policies retain
  their contracts. Coverage preservation is limited by mip quantization; there
  is no promise of exact subpixel silhouettes or model-LOD crossfading.
- `MeshInstanceBatch3D` compares and hashes complete override values rather than
  splitting identical override sets by component address. Source values remain
  immutable during submission; different values still form different groups.

## Example/source changes

The near grass now uses the restored opacity and neutral white mesh tint. Its
material overrides enable leaf transmission, reduce normal perturbation and
provide AO control. The tree also uses neutral mesh tint. Ground tint and ambient
intensity were calibrated, the exposed-rock material now uses the installed pale
Marble Cliff 05 maps, and snow now uses the installed Snow 02 maps instead of a
flat scalar surface. These are source/material changes, not terrain geometry or
collision changes. The package installations remain unchanged.

The distant grass card was rebaked at 1024px with alpha preserved. At identical
bake resolution/framing, the unrepaired source produced 71,536 covered pixels,
including 11,273 near-black covered pixels. The corrected source produced 47,981
covered pixels and zero near-black covered pixels. Here covered means alpha >127;
near-black means all RGB channels <16. Those metrics concern the albedo bake,
not final lit scene brightness. Foliage density was not increased to hide the bug.

Reproducible background tools:

- `scripts/art/restore_gltf_opacity.py` connects the original opacity and exports
  packed RGBA glTF while retaining cutout semantics.
- `scripts/art/bake_albedo_impostor.py` bakes albedo/alpha without directional
  lighting, AO or normal-map shading. The existing cluster bake helper also
  preserves its source material's alpha graph rather than replacing it with
  opaque emission.

Use ordinary asset import/reimport afterward. Generated/cooked outputs were not
hand-edited. Raw diffuse/opacity inputs and their provenance remain source data.

## Matched maximized capture

NVIDIA RTX 3070 Ti Laptop GPU, driver 615.71.09, Vulkan, Release. Same opening
camera and default dock layout; 5120 x 2742 maximized window, 2732 x 1710 terrain
viewport. Each capture lasted 100 seconds; values use the last 500 frames.

| Scope | Before median / p95 | Final median / p95 |
| --- | ---: | ---: |
| Frame interval | 19.275 / 20.420 ms | 19.417 / 20.519 ms |
| Editor CPU frame | 18.870 / 20.030 ms | 19.005 / 20.054 ms |
| Viewport CPU | 16.700 / 17.709 ms | 16.861 / 17.722 ms |
| GPU | 12.522 / 14.199 ms | 14.489 / 15.295 ms |

Frame time was effectively unchanged in these single matched captures; GPU work
increased. This is a quality/cost measurement, not a speedup claim or universal
FPS guarantee. The final masks show actual grass silhouettes instead of black
polygon backing. The coarseness of the landscape, repetitive composition, limited
indirect lighting and discrete vegetation LOD appearance remain visible gaps.
Regional normal filtering was inspected but left unchanged; changing its
footprint requires separate seam/morph and biome-rule qualification.

Local evidence: `/tmp/demi-quality-before.png`, `/tmp/demi-quality-final.png`,
`/tmp/demi-quality-before.csv`, `/tmp/demi-quality-final.csv`. The intermediate
lighting-only capture still showed the black polygons and was not accepted as
the final result.

## Verification

- All 87 selected material/texture/terrain/editor/render checks pass with current
  sources. The broader run found a material-set fixture using only authored
  manifests; it now uses the normal package-aware registry and passed its rerun.
  This is not a full repository test run.
- Added tests cover parameter domains, uniform values, AO packing, equivalent
  override batching, cutout color/coverage filtering, and imported material
  cutoff handling. Existing shader variants compile for GLSL, GLES and SPIR-V.
- All four visible desktop terrain E2E scenarios pass using the unattended
  in-engine camera route. No desktop input injection is required.
- Project validation checked 203 files without diagnostics; `git diff --check`
  is clean. The previously documented headless E2E limitation is not qualified
  by these visible tests.

```sh
cmake --build --preset linux-release --target demi-editor demi
ctest --test-dir build/linux-release --output-on-failure \
  -R 'terrain|material|texture-library2d|opaque-instance-plan|editor.*hierarchy|editor-scene-view-state|editor-asset-workflow|bgfx-renderer3d|scene-visibility3d|bgfx-gpu-resources|bgfx-graphics-device'
./build/linux-release/demi validate examples/terrain_3d
./build/linux-release/demi test linux --project examples/terrain_3d --timeout 300
```
