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

Builds normally compile bgfx's `shaderc` host tool automatically. Offline,
cross, and sanitizer build trees can reuse an existing executable with
`-DDEMI_HOST_SHADERC=/absolute/path/to/shaderc`; CMake validates the path and
does not rebuild the shader toolchain.

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

`Environment3D` owns ambient light, fog, shadow distance/resolution, and the
maximum number of shadow-casting lights. `DirectionalLight`, `PointLight`, and
`SpotLight` support color, intensity, masks, and bounded shadow participation.
The lightweight forward path evaluates at most four lights per camera and
honors the environment shadow-pass budget.

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
- `Renderer3D.stats.shadow_passes`
- `Renderer3D.stats.render_target_bytes`

Keep mobile particle budgets and camera target sizes conservative. A minimap
adds another world pass even when its target is physically small, so give
secondary cameras a deliberate `update_interval`.
