# bgfx Renderer Migration

DemiEngine's raylib-to-bgfx migration is complete. The runtime uses:

- bgfx for GPU devices, resources, draw submission, render targets, and
  renderer selection;
- SDL3 for Linux and Android windows, events, input, clipboard, display
  information, and native window handles;
- miniaudio for audio, unchanged;
- DemiEngine-owned asset, scene, material, and gameplay APIs above those
  infrastructure boundaries.

Game code and serialized scenes must not contain bgfx or SDL types.

## Current status

| Phase | Status | Remaining cutover |
| --- | --- | --- |
| 1 | Complete | None |
| 2 | Complete | None |
| 3 | Complete | None |
| 4 | Complete | None |
| 5 | Complete | None |
| 6 | Complete | None |

## Ownership boundaries

`GraphicsDevice` is the engine-facing GPU lifecycle contract.
`BgfxGraphicsDevice` is the only module allowed to own bgfx initialization,
reset, frame submission, and shutdown. Renderer modules will consume narrow
resource and command interfaces layered above it rather than include bgfx in
scene, scripting, physics, or asset-format code.

`PlatformHost` owns windows and input. Its SDL3 implementation translates SDL
events into the existing `InputState` and provides `NativeWindowHandle` to the
graphics device. This prevents another renderer change from also rewriting
gameplay input and application lifecycle code.

Linux and Android 2D scenes now enter through `Bgfx2DAppHost`, which owns the
shared `BgfxAppContext`. The context owns the SDL window, bgfx device,
resources, and command submission in a strict shutdown order. The 2D and 3D
hosts compose their renderers above that common boundary instead of owning a
second device lifecycle. `RuntimeApp` only coordinates simulation and forwards
the resulting world. The `DEMI_GRAPHICS_API` environment variable can select
`automatic`, `vulkan`, `opengl`, `opengles`, or `noop` for diagnostics.

## Migration phases

### 1. Graphics-device foundation

- Add the backend-neutral `GraphicsDevice` contract.
- Add bgfx lifecycle ownership with Automatic, Vulkan, OpenGL, OpenGL ES, and
  Noop selection.
- Exercise initialization, resize, frame submission, invalid configuration,
  and repeated shutdown through the headless Noop backend.

This was the temporary foundation stage; visible rendering now uses bgfx.

### 2. SDL3 platform host

- Move window, input, touch, gamepad, clipboard, DPI, focus, suspend, and
  Android lifecycle handling out of `RuntimeApp`.
- Implement Linux and Android SDL3 native-window extraction.
- Initialize `BgfxGraphicsDevice` with Vulkan by preference and a documented
  fallback selected from bgfx capabilities.
- Add event translation and lifecycle tests independent of a GPU.

### 3. Shader and GPU resource cooking

- Use bgfx `shaderc` as a host build/cook tool.
- Replace runtime GLSL compilation with cooked backend binaries.
- Add texture, sampler, buffer, program, framebuffer, and render-target owners
  with generation-checked engine handles.
- Preserve shader asset IDs and material data while removing
  `platform_sources` from normal authoring.

Unified shader assets use bgfx's cross-backend shader language:

```json
{
  "format_version": 1,
  "vertex": "surface.vs.sc",
  "fragment": "surface.fs.sc",
  "varying": "varying.def.sc"
}
```

`demi cook` compiles that one source pair to Vulkan SPIR-V plus the native
fallback for the requested package: desktop OpenGL for `linux`, or OpenGL ES
for `android`. The generated `cook.manifest.json` records each program by
stable asset ID and backend. Legacy `platform_sources` is rejected.

### 4. 2D, UI, and text

- Port sprites, tilemaps, particles, debug primitives, cameras, HUD nodes,
  font atlases, clipping, and nine-patch rendering.
- Batch compatible 2D draws and expose batch/draw statistics.
- Move all 2D and UI examples to bgfx and validate Linux and Android output.

The backend-neutral `Canvas2D` path now submits generation-checked transient
draws through bgfx and batches compatible quads and triangle primitives.
Texture decoding/upload/replacement, transformed and masked sprites, animated
tilemaps, retained UI nodes, scroll clipping, scalable TTF glyph atlases,
nine-patch geometry, isometric grids/entities, particles, GIF and image-frame
animations, navigation and physics debug overlays, production image formats,
and per-frame draw statistics have Noop-backed regression coverage.
`BgfxRenderer2D` now composes those specialized renderers behind one
runtime-facing API and owns asset reload, frame sequencing, and shared
resources without exposing bgfx types. SVG assets are rasterized
through a backend-neutral RGBA decoder when librsvg is available. The visible
2D runtime now uses SDL3 and bgfx on Linux and Android; native-window metadata
distinguishes Wayland from X11 so Vulkan and OpenGL create the correct surface.
The Android package uses SDL's activity/JNI bootstrap rather than the retired
NativeActivity window bootstrap.

### 5. 3D and effects

- Port static and skinned meshes, animation palettes, lighting, particles,
  camera surfaces, post-processing, world text, debug geometry, and
  instancing.
- Cook model and texture data without raylib resource types.
- Move all 3D and voxel examples to bgfx with performance budgets.

The backend-neutral render-command layer now covers perspective and
orthographic views, depth/cull/blend state, primitives, static/imported meshes,
CPU-skinned animation, model material colors/textures, authored materials,
lighting, billboard particles, world text, camera targets, post-processing,
and static-mesh instancing. `Bgfx3DAppHost` owns the same SDL/bgfx lifecycle on
Linux and Android, with Noop-backed ownership and failure tests.

### 6. Removal

- Make bgfx the only runtime renderer.
- Remove raylib, `rlgl`, generated GLSL headers, the raylib filesystem bridge,
  and raylib resource types.
- Remove temporary dual-backend build paths.
- Run every example, Linux package test, Android package test, and sanitizer
  lifetime suite.

## Completion rule

Linux and Android now use bgfx for every visible example, and runtime/public
source contains no raylib renderer adapter.
