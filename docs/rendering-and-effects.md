# Rendering And Effects

Presentation is scene- and asset-authored. Gameplay Lua does not call a
graphics backend, and adding a light, minimap, particle effect, or color grade
does not require a renderer branch.

## Materials and shaders

A `Material` asset selects a shader, fallback, texture slots, numeric/color
parameters, and render state. `MeshRenderer.material_properties` overrides
parameters for one entity without cloning the material asset.

```json
{
  "format_version": 1,
  "shader": "builtin://lit",
  "fallback": "builtin://unlit",
  "textures": { "albedo": "asset://textures/terrain" },
  "parameters": { "base_color": [1, 1, 1, 1] },
  "render_state": {
    "blend": "opaque",
    "cull": "back",
    "depth_test": true,
    "depth_write": true,
    "alpha_cutoff": 0.0
  }
}
```

`alpha_cutoff` discards texels whose alpha is below the configured value. It
defaults to `0.0` and accepts values through `1.0`; pixel-art foliage and other
opaque cutouts commonly use `0.5` so transparent atlas pixels do not write
color or depth.

Custom `Shader` assets name one bgfx shader-language source pair and a varying
definition. Built-in shaders are embedded in the executable.

```json
{
  "format_version": 1,
  "vertex": "outline.vs.sc",
  "fragment": "outline.fs.sc",
  "varying": "outline.varying.def.sc"
}
```

`demi run` cooks shader-bearing source projects before the renderer starts;
Linux and Android packages cook them during packaging. `shaderc` emits Vulkan
plus OpenGL on Linux, or Vulkan plus OpenGL ES on Android, and the renderer
selects the matching binary by stable `asset://` ID. Invalid or missing
programs fail startup with diagnostics instead of silently changing a
material's appearance.

The renderer's default 3D material automatically selects a directional-only
fragment variant when the camera's resolved lighting has no active point or
spot lights. Ambient/directional light, texture, alpha cutoff and debug output
are unchanged. Positive-range, positive-intensity local lights select the full
shader immediately; custom material programs are never substituted. No asset
flags or project settings are required for this optimization.

Builds normally compile bgfx's `shaderc` host tool automatically. Offline,
cross, and sanitizer build trees can reuse an existing executable with
`-DDEMI_HOST_SHADERC=/absolute/path/to/shaderc`; CMake validates the path and
does not rebuild the shader toolchain.

Built-in 3D cube, sphere, cylinder, and plane meshes share resident geometry
and are automatically instanced when shape, texture, and color match. Large
collections of primitive entities therefore keep normal entity authoring and
individual transforms without requiring one draw call or mesh upload each.
CPU-skinned models retain dynamic vertex buffers across poses; animation
updates buffer contents while immutable index data remains resident.

## Cameras and render targets

Every enabled `Camera3D` is rendered in ascending `priority` order. `primary`
selects the camera used by gameplay-facing camera queries; it does not change
pass order. A camera supports a normalized viewport, render mask, clear mode,
post stack, and optional `render_target`.

`render_scale` changes the 3D pass resolution without scaling the final HUD;
values below `1.0` trade some world resolution for fill-rate headroom, while
values above `1.0` supersample. Explicit render-target assets retain their
authored dimensions.

`update_interval` lets secondary cameras reuse their last completed target
between refreshes. The cached target is still composited every frame, and the
camera renders immediately on its first frame, after its surface is discarded,
or when a window resize changes the required surface size. This is useful for
minimaps and surveillance displays; gameplay cameras should normally keep the
default `0.0` interval.

Set `render_hud_to_target` to render the scene HUD into that camera target.
That target is then addressable as a material texture, which is the lightweight
world-space UI path. `render_hud` controls the final screen HUD separately.

## Lighting and environment

`SurfaceRelief3D` generates height-map box relief in a bounded session cache and
shares meshes across instances. It writes no generated model files. See
[runtime relief and limits](streamed-destruction.md#relief-without-generated-assets).

For primitives and imported models alike, `MeshRenderer.texture` overrides a
material's albedo texture, which overrides an imported model's embedded albedo.
With none specified, rendering uses white. Static instancing groups include the
resolved texture, so different overrides are not batched under the wrong image.

The built-in 3D shader decodes the combined display-sRGB base color before
multiplying it by linear lighting, then encodes the result for the UNORM scene
target. Previously this multiplication happened directly in display space,
making unlit faces excessively dark. This applies to both directional-only and
local-light shader variants, including editor scene targets; HUD and diagnostic
colors are unchanged. Alpha is not gamma-corrected. Unit illumination preserves
the original base color.

This corrects the existing forward color path: light colors remain linear
coefficients, the combined base-color convention is retained, bright output can
still clip on the LDR target, and post effects still operate on the encoded
scene image. Imported material-factor
color spaces, linear texture filtering/blending and HDR tone mapping remain
separate work.

### Anti-aliasing

3D scenes use **4× MSAA by default**, including standalone rendering, the editor
Viewport and embedded Game View. This also applies when there is no
`Environment3D` entity.

Set `Environment3D.msaa_samples` to `0` (off), `2`, `4`, `8`, or `16`:

```json
"Environment3D": {"msaa_samples": 8}
```

The Inspector offers these values as a dropdown. Author the field if it is
currently showing its default. Settings take effect on subsequent rendered
frames; no restart is needed. Named camera targets change sample mode on their
next content update, preserving the previous image until then.

Standalone cameras share a multisampled backbuffer. Editor images and scaled
or post-processed scenes use multisampled color/depth attachments and resolve
the color image before presentation. Shadow maps remain single-sampled and use
their own filtering. 2D-only scenes keep their existing rendering path.

More samples use more GPU memory and bandwidth. Hardware may use fewer samples
than requested; formats without multisample support fall back to single-sample
rendering. Profiler gauges `Renderer3D.msaa_requested` and
`Renderer3D.msaa_backend_request` show the requested/submitted modes, not a
measurement of the driver's final sample count.

MSAA smooths geometry edges. It does not add polygons, increase texture detail,
or raise shadow-map resolution. `Camera3D.render_scale` still controls scene
resolution independently.

### Real-time directional shadows

The maintained authoring contract is in the website's
[directional shadow guide](https://demiengine.de/docs/gameplay-3d#directional-shadows).
The shared renderer now uses configurable per-camera cascades and continuously
weighted PCF. Vulkan barrel captures and renderer/layout tests cover this change;
physical Android shadow qualification, point/spot shadows and custom vertex
deformation remain open. See `plan.md` for the remaining rendering work.
