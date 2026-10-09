# NVIDIA HUD lifecycle investigation

Investigated on 2026-10-09, after `0c6c354`, on an NVIDIA GeForce RTX 3070 Ti
Laptop GPU with driver 615.71.09. Native editor: SDL3/XWayland, Vulkan, external
5120x2880 display. The editor window was 5120x2806. NVIDIA was selected only for
the test process via `VK_DRIVER_FILES`; system settings were unchanged.

## Reproducer

The checked-in fixture `tests/fixtures/editor_hud_lifecycle` contains only a
project, two scenes and a HUD. Its main scene has one camera and one solid HUD
panel. There are no scripts, fonts, terrain assets, meshes, physics bodies or
package dependencies. The control scene has the camera without the HUD.

```sh
./build/linux-release/demi validate tests/fixtures/editor_hud_lifecycle
./build/linux-release/demi-editor --project tests/fixtures/editor_hud_lifecycle/demi.project.json
```

Repeat Play, Stop, then click Viewport, allowing several frames between each
operation. The recorded native runs used roughly 0.6 seconds per action.
The minimal main scene crashed within two cycles on the affected NVIDIA path.
For a no-HUD comparison, use a temporary copy and set its project `main_scene`
to `scene://hud-lifecycle/control`. Merely opening the control scene in the
Viewport does not change the project's Play start scene.

This fixture has no Linux-specific authored data. The same manual test should
be used for Windows qualification once the editor is ported. Windows has not
been tested; these observations do not establish whether the fault is OS-,
driver- or backend-specific.

## Isolation results

| Case | Result on NVIDIA Vulkan |
| --- | --- |
| Camera-only scene, no HUD | Survived 20 Play/Stop/Viewport cycles |
| Camera plus original static colony HUD | Crashed within two cycles |
| Camera plus one solid panel, no text | Crashed within three cycles |
| Four-file fixture, normal editor build | Crashed within two cycles |
| Same fixture with a 2D camera instead | Survived 12 cycles |
| 3D fixture with `Environment3D.msaa_samples: 0` | Survived 16 cycles |
| Panel with synchronization validation enabled | Crashed within two cycles; no frame-resource validation error reported |
| Temporarily retain Canvas2D program references | Still crashed within two cycles |
| Temporarily retain Canvas2D white textures | Still crashed within two cycles |
| Temporarily retain Canvas2D sampler references | Still crashed within two cycles |
| Temporarily retain framebuffers and their attachment references | Still crashed within two cycles |
| Run bgfx on the calling thread | Still crashed within three cycles |
| Retain the complete Game renderer across Play sessions | Survived 16 cycles |

A surviving run is not proof of correctness. All resource-retention and
single-thread diagnostic edits were removed and the normal editor rebuilt.
No deliberate leaks, disabled renderer threading, GPU-selection policy or
platform-specific rendering workaround is part of this investigation's commit.

Earlier full-colony runs survived 24 cycles with basic validation and 12 on AMD
Vulkan. The newer synchronization-validation crash shows that validation is not
a reliable workaround. Validation reported the separate upstream bgfx startup
warning `VUID-VkDeviceCreateInfo-ppEnabledLayerNames-12385`; it has not been
established as causal.

## Evidence and interpretation

The crash stack enters `libnvidia-glcore.so.615.71.09` from
`bgfx::vk::RendererContextVK::submit`, at `vkCmdDrawIndexed`. In the panel-only
core, the saved draw index count was six, consistent with a quad. That alone
does not distinguish the HUD quad from an editor image quad.

Removing geometry and text does not prevent the fault. Removing the HUD does,
within the tested run. Keeping the whole renderer alive changes the result;
retaining individual tested resources and removing the render thread do not.
The 2D and MSAA-off comparisons further narrow investigation to the 3D HUD
multisampled render/resolve path during renderer recreation, including shared
backend state/cache reuse. The 3D default is four samples. It does not yet prove a specific
use-after-free or assign responsibility to Demi, bgfx or NVIDIA.

Useful local evidence (not committed binaries): systemd cores for PIDs 50755,
51699, 52547, 53851, 54866, 56018, 57159, 60228 and 61168. Logs are under
`/tmp/demi-nvidia-*.log`; the panel draw stack is `/tmp/demi-panel-stack.log`.
Temporary validation libraries were unpacked below `/tmp/demi-vulkan-validation`,
not installed system-wide. The normal build should reproduce without them.

## Next investigation boundary

Compare teardown of the complete 3D renderer, its HUD overlay and MSAA scratch
target/resolve pass separately, then inspect bgfx descriptor/pipeline/render-pass
cache reuse between instances. The clean 2D and MSAA-off runs should remain
controls. Disabling MSAA was a diagnostic scene override, not a new default. A standalone GPU lifecycle test with the same
quad would help separate editor orchestration from renderer/backend behavior.

Do not ship whole-renderer retention just because this experiment survived:
a production reuse design must invalidate project assets, reset world-specific
caches and release resources predictably. Prefer a shared lifecycle/cache fix
that also applies on Windows. Any genuinely NVIDIA/Vulkan-specific correction
belongs behind the graphics adapter and needs documented scope plus unaffected
backend coverage. Do not change graphics API or driver defaults to hide the bug.
