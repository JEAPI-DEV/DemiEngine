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

Set `DirectionalLight.casts_shadows: true` to render a shadow map whenever the
camera content updates. Moving objects and detached masonry use their current
geometry and transforms. The shared renderer supports this in standalone Play,
Game View, and the scene Viewport.

`Environment3D` controls map quality:

- `shadow_resolution`: square map size, default 1024. Unsupported device sizes
  fail with a diagnostic; values are not silently clamped to 4096.
- `shadow_distance`: coverage radius in metres around a point ahead of the
  camera, default 80. Smaller coverage gives more detail at the same resolution.
- `shadow_bias`: depth offset in metres, default 0.02. Filtering adjusts samples
  for the receiving surface's slope to reduce self-shadow striping.
- `max_shadow_lights: 0` or `shadow_distance: 0` disables the pass.

The current implementation shadows the selected directional light, matching
the existing single-directional-light shading path. Larger light budgets do
not add point/spot shadow maps. Those requests produce validation warnings.
Maps use 3×3 filtered comparisons and independent per-camera targets. Geometry
outside the camera view can cast into it when inside the shadow volume.

Opaque meshes and alpha-cutout materials cast shadows. Transparent shadow casting,
custom vertex-shader deformation, cascades and point/spot shadows are not
implemented. Custom fragment shaders need their own receiver support. Shadows
add a geometry pass; they are opt-in and remain off by default.

Try [the moving-shadow example](../examples/shadows_3d/README.md). Directional
shadows are also enabled in the destruction weapon lab. Visual checks cover
desktop Vulkan at 1080p; Android shadow qualification remains open.

### Panorama sky background

Texture manifests with `settings.mipmaps: true` now upload a complete RGBA8
box-filtered mip chain. Linear filtering interpolates between mip levels;
nearest filtering selects the nearest level. This reduces distant texture
aliasing without changing UVs or mesh geometry. Filtering currently averages
encoded source channels; linear-light/premultiplied-alpha filtering is future
work.

Set `Environment3D.sky_texture` to a `Texture2D` asset containing a 2:1
equirectangular, tone-mapped panorama (JPEG/PNG). It uses the normal scene asset
loading path. The camera-centered background renders unlit at the far plane,
without writing depth, so moving the camera creates no sky parallax and scene
geometry always remains in front. The same pass is used by the editor and game.
Only perspective cameras with `clear_mode: "color"` draw a sky; orthographic
and overlay cameras retain their existing clear behavior. Omit the reference
to keep the camera's solid background. The last enabled Environment3D wins,
matching ambient-environment selection.

This is a visible LDR background. EXR decoding, HDR lighting, reflection
probes, and image-based lighting are separate work. Sun and ambient
illumination remain explicit.
The destruction weapon lab uses the supplied evening panorama plus Kenney
prototype grid textures on its floor and rear wall.

## Particles

`ParticleEmitter2D` and `ParticleEmitter3D` provide point/area emission,
continuous rate plus burst, lifetime, velocity, gravity, size and color
transitions, rotation, sorting, deterministic seeds, pooled storage, and
separate desktop/mobile budgets. Simulation advances once per frame; extra
cameras only filter and draw existing particles. A non-looping emitter is a
one-shot burst; stopping and starting it explicitly arms that burst again.

## Post effects and text

Attach `PostProcessStack` to a camera for exposure, contrast, saturation,
tint, vignette, thresholded bloom, and fade. `WorldText3D` renders labeled
world objects with distance and render-mask filtering.

## Diagnostics

`demi run linux --profiler` and profiler reports expose:

- `Renderer3D.stats.batches`
- `Renderer3D.stats.triangles`
- `Renderer3D.stats.particles`
- `Renderer3D.stats.lights`
- `Renderer3D.directional_shadow` (CPU preparation/submission scope)
- `Renderer3D.shadow_batches` (most recently rendered camera's shadow draws)
- `Renderer3D.stats.render_target_bytes`

Keep mobile particle budgets and camera target sizes conservative. A minimap
adds another world pass even when its target is physically small, so give
secondary cameras an explicit `update_interval`.
