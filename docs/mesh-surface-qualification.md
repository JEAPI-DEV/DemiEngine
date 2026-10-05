# Mesh surfaces and near-camera picking

## Contract and ownership

MeshRenderer has optional `metallic`, `roughness`, `opacity` and `surface_mode`
overrides. Scalars are finite 0–1 values; modes are opaque, transparent or
additive. Omission inherits an assigned Material asset, otherwise the built-in
fallback is 0/.8/1/opaque. These inherited values are not invented serialized
component defaults. Reflection, canonical schema, native parsing/mutation,
Inspector controls and Entity Lua annotations agree on the contract.

`MeshSurface3D` resolves material values, built-in `material_properties` and
explicit component overrides into a per-draw surface. Opaque batch keys use
exact float bits so different parameters do not alias through decimal formatting.
`MeshDrawOrder3D` keeps opaque extraction order and sorts only the transparent
suffix by camera depth. Opaque groups flush before transparency; each visible
mesh traverses its draw path once. Transparent draws disable depth writes and
use sequential submission. Vertex/texture alpha, entity tint, material base
colour alpha, opacity and fragment fades multiply.

The built-in lit shader uses direct Cook–Torrance lighting with GGX, correlated
Smith visibility and Schlick Fresnel, following the
[Filament standard model](https://google.github.io/filament/main/filament.html).
The stable GGX denominator preserves narrow highlight peaks instead of clipping
them with a broad numerical epsilon. ESSL 100 uses explicit highp; GLSL 140 and
SPIR-V profiles remain in the existing build. Unlit assets/models bypass surface
lighting, and the lighting-debug output retains incident directional lighting.
Material base colour now contributes to the built-in draw tint.

Glass is alpha blending plus direct specular light, not refraction or glass
transmission. Environment reflections/IBL, general PBR texture maps, and arbitrary
per-entity custom-shader uniform overrides remain outside this change. Metals
under ambient light alone appear dark. Intersecting transparency and triangle
ordering within one mesh remain approximate; transparent surfaces do not cast
ordinary opaque shadows.

## Picking

Picking intersects the ray/box interval with the camera's visible near/far depth
range. A visible volume can be selected when the eye or near plane is inside its
bounds. Fully clipped geometry and near-clipped camera/light/empty proxies do not
steal clicks. Gizmo lengths derive from view depth, FOV, projection and viewport
height, giving constant screen size without a minimum world-size floor. Picking
continues to use bounds rather than exact imported-mesh triangles.

## Verification

Release editor and CLI builds compile the GLSL, ESSL and SPIR-V shader outputs.
Eight focused tests pass (3.65 seconds): viewport tool, Inspector model, workspace,
prefab components, scene loader, component schema, material library and bgfx 3D
renderer. Coverage includes inside/near-clipped picking, clipped proxies,
constant handle size, strict optional-field parsing, reflected field mutation,
inheritance/reset presentation, exact surface batching, draw-state/order and
polished GGX boundary values. The prefab UI fixture scrolls its real Inspector
before activating the Add Component control.

An isolated material scene validates and runs headlessly for three frames.
`terrain_graph_3d` (4 files) and `destruction_weapons_3d_lab` (87 files) validate
without diagnostics. A PID/title-verified native Vulkan capture at 5120×2806
visually confirms the metallic highlight, alpha pane and Inspector controls;
it is published as `public/images/docs-mesh-surface-controls.png`.

This is a scoped desktop gate, not full-suite, Android-device, arbitrary material
or intersecting-transparency qualification. Website templates were updated but
not rendered or deployed. Existing user scene data and their commit were left
intact; `git diff --check` is clean.
