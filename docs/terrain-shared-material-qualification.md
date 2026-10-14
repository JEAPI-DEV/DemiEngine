# Shared terrain and mesh materials

Date: 2026-10-14. Build: `linux-release`. Native checks: Vulkan through SDL/X11.
Prepared terrain payload is now version 8 because the obsolete unsupported-role
field was removed. Generator version remains 9; source format versions remain
unchanged. Regenerate derived terrain and recook existing outputs.

## Delivered Milestone 4 boundary

The user chose one Material workflow. Ordinary format-1 Material assets now own
PBR maps, UV/triplanar mapping and normal-map conventions. The separate
`terrain_material` DataAsset parser, creation option and Inspector route have
been removed. Material sets remain terrain role-to-Material bindings; palettes
remain surface-set/placement data. Existing repository example surfaces already
used ordinary Materials, so their source IDs and art did not need replacement.
Native test fixtures were migrated to the canonical contract.

`MaterialSurface` is a typed projection of the existing Material descriptor,
not a second authored representation. Builtin fields have shared domain checks;
custom shaders retain their own parameter/texture vocabulary, with finite-float
validation. Material import/reimport validates references and infers map/shader
dependencies before publishing a manifest. Failed reimport preserves previous
metadata and generated content.

The material editor exposes shared surface settings and map pickers through the
existing document, Undo/Redo, Save and reimport paths. Clearing a detail-normal
map also clears its dependent strength in one command. Opening an asset focuses
its document tab. The old flat circle is now explicitly a base-color swatch;
a real shaded preview remains recorded in `tofix.md`.

## Rendering and sampling

Builtin mesh materials support albedo, normal, roughness, metallic, ambient
occlusion, detail-normal and emissive maps. Roughness/metallic maps replace their
scalar fallbacks; explicit mesh overrides remain authoritative. A per-mesh
tiling override can activate mapped shading even when the asset itself uses
default tiling. Custom shader programs do not enter this builtin adapter.

Normal convention is explicit OpenGL (+Y) or DirectX (-Y), independent of OS or
backend. Normal maps affect lighting; the geometric receiver normal continues
to own shadow correction. UV mapping uses authored UVs. Triplanar projection
uses local surface units including MeshRenderer.size, before Transform scale,
and respects terrain biome texture scale. It uses three projections per active
map. Height is retained with an explicit warning but is not yet sampled; there
is no claimed geometric displacement or collision change.

Shared image mip generation distinguishes linear scalar channels, sRGB colors
(averaged in linear light) and normal vectors (renormalized after averaging).
Bilinear sampling still filters stored sRGB bytes before shader decoding;
hardware sRGB filtering remains a Milestone 4 quality task.
GPU variants are created for actual semantic usage, rather than duplicating all
images into every encoding. Extra dynamic usages can load another variant once;
only source descriptors are retained, not a second CPU image library. A failed
variant upload leaves an existing valid variant intact. Texture wrap settings
remain authored; Clamp warns on tiled triplanar materials, while Repeat and
Mirror are valid.

The mapped mesh path uses nine sampler slots. Unsupported devices report that
requirement rather than silently removing maps. UV surfaces sample up to seven
maps, triplanar up to twenty-one texture reads. Shadow casting needs albedo
coverage only. No performance improvement is claimed.

Textured terrain currently follows the ordinary mesh compatibility path. The
GPU patch path remains the solid-palette implementation. Height-aware layers,
GPU texture arrays/batching, residency budgets based on real map dimensions,
macro variation and licensed starter art remain open Milestone 4 work.

## Reproducible native probe

`scripts/generate_material_surface_probe.py` writes a fresh temporary project
using only Python's standard library. Diagnostic PNGs are authored checker,
groove and channel patterns, not a claim of realistic starter art. Terrain
roles, a cube and a sphere reuse the same Material asset in each UV/triplanar
column. Every role is bound; no explicit biome-material override is needed.

```sh
python3 scripts/generate_material_surface_probe.py /tmp/material-probe
python3 scripts/generate_material_surface_probe.py /tmp/material-directx --normal-convention directx
python3 scripts/generate_material_surface_probe.py /tmp/material-flat --normal-strength 0 --detail-strength 0
./build/linux-release/demi validate /tmp/material-probe
./build/linux-release/demi cook --project /tmp/material-probe --output /tmp/material-cooked
```

Native 3840-by-2022 captures show the shared material on terrain and objects;
triplanar projection maintains pattern scale on steep faces. Disabling normal
and detail maps changes lighting without regenerating geometry. OpenGL/DirectX
versions, with correspondingly encoded source maps, differ by less than 0.001
mean RGB channel levels over the image on the 0–255 scale. Their tiny residual
comes from byte normal encoding. The flat-normal control differs by roughly
2.5–3 channel levels over the full image (including its large background).

The cooked/native capture is pixel-identical to the source/native capture.
The final payload-8 comparison also matches exactly at 3413-by-1917
(`/tmp/material-source-v8.png` and `/tmp/material-cooked-v8.png`). A bounded
100-frame Vulkan run exits normally, and a fresh rocky-valley capture confirms
the previous shadow-grid fix remains intact (`/tmp/material-shadow-regression.png`).
Temporary evidence: `/tmp/material-surface-auto.png`,
`/tmp/material-directx.png`, `/tmp/material-flatnormals.png`,
`/tmp/material-cooked.png` and `/tmp/material-editor.png`. The final editor
capture is 5120-by-2806 and shows the automatically focused Material Editor
with map, convention, projection and scalar controls.
Native windows were run one at a time; the user's editor was saved and closed
with their agreement before these checks.

The first Vulkan launch caught a real shader interface mismatch: expanded mesh
vertex outputs were not yet declared by the legacy lit fragment wrapper. Both
legacy and mapped wrappers now agree, including directional and skinned
variants. Noop placeholder programs alone would not have caught it. A new regression
links all twelve real generated SPIR-V mesh/fragment combinations through
bgfx’s common interface validation under Noop, without opening a window.

## Validation scope

Seventeen focused suites pass. They cover Material parsing/binding, reference dependencies,
transactional import/reimport, texture mip semantics and variant lifetime,
terrain validation/palettes/cooked assets, GPU solid-path eligibility and editor
creation/controls/document commands. All 32 repository example projects validate.
The diagnostic source validates 20 files; its cooked output validates 18.
Three-frame source and cooked headless runs pass.

Native checks qualify Linux Vulkan. Shader variants compile for SPIR-V, GLSL
and ESSL, but Windows and physical Android rendering have not been qualified.
Website sources are updated locally; PHP lint and publication were not run.
Milestone 4 is still in progress.

## Optional solid surface blending

`Terrain3D.surface_blending` defaults to true and is exposed as **Surface
Blending** in the Inspector. False selects a single original cell-triangle
material instead of interpolating neighboring solid palette entries. It does
not change geometry, normals, LOD selection or texture filtering. Mapped
materials on the ordinary mesh fallback remain discrete in either setting.

Linux Release Vulkan captures of isolated rocky-valley copies at 3413 x 1917,
using the same camera, recipe and lighting, show sharp snow/rock and grass/scree
boundaries with blending disabled. Only one native process ran at a time.
Session evidence is `/tmp/demi-blending-on.png` and
`/tmp/demi-blending-off.png`; these temporary captures are not shipping assets.
The example has solid palette colors and four-unit cells. Blending stretches
color transitions across those cells; disabling it exposes their triangular
boundaries. Surface detail and height-aware blending remain separate work.

Five Release regression suites pass: component schema, real shader interface
linking, GPU field data, patch batching and terrain workspace history. They cover
the default and strict boolean contract, shader triangle selection, live flags
on shared batches, unchanged field/mesh/collision resources, inherited prefab
values, failed Undo rollback and source-preserving Save. Both comparison projects
and the unchanged rocky-valley example validate without diagnostics.
The rebuilt native Release editor also displays the unchecked Surface Blending
control for the sharp comparison scene (`/tmp/blending-editor.png`). Its saved
narrow Inspector layout wraps field labels heavily; that is separate from the
terrain rendering change.
