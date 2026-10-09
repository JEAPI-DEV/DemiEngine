# Deferred editor fixes

## Intermittent NVIDIA Vulkan crash around embedded Play/Stop

Observed 2026-10-09 while testing `examples/colony_builder`, on the external
5120x2880 display under XWayland. Multiple editor processes crashed in
`libnvidia-glcore.so.615.71.09` from `bgfx::vk::RendererContextVK::submit`, at
`vkCmdDrawIndexed`. The main thread was waiting in `bgfx::frame`.

Reproduction: repeatedly Play, Stop, and return to Viewport. A crash also
occurred after Stop while attempting to edit a Lua numeric property, before
that edit reached disk. This is not confirmed as a property-editor fault.

Submitting the ImGui frame before releasing the stopped Game renderer improves
resource lifetime ordering but DOES NOT resolve this crash: the modified build
still crashed after five Play/Stop cycles on the default NVIDIA path.

Diagnostic comparisons using process-local environment overrides:

- Khronos validation enabled: eight Play/Stop cycles plus sixteen
  Play/Stop/Viewport cycles survived. No frame-resource VUID was emitted.
  Startup emitted VUID-VkDeviceCreateInfo-ppEnabledLayerNames-12385 concerning
  bgfx's legacy device-layer list. This warning is not established as causal.
- AMD-only Vulkan (`VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_icd.json`):
  twelve Play/Stop/Viewport cycles survived. Editing power supply and running
  the resource simulation also worked.

Core evidence remains in systemd-coredump for PIDs 28923, 30268 and 34113.
Temporary investigation logs: `/tmp/demi-property-main-bt.log`,
`/tmp/demi-lifetime-validation.log`, `/tmp/demi-lifetime-amd.log`.
The validation layer was unpacked below `/tmp`, not installed system-wide.

Needs deeper NVIDIA/bgfx investigation, including resource reuse and timing.
Do not mark fixed based only on the AMD or validation-enabled runs, and do not
silently change the production backend or GPU to hide it.

Follow-up isolation and a portable four-file reproducer are recorded in
[the NVIDIA HUD lifecycle investigation](docs/editor-nvidia-hud-lifecycle-investigation.md).
The normal editor reproduces with only a camera and one solid HUD panel;
terrain, text and gameplay are not required. Single-thread rendering and
individual resource-retention experiments did not resolve it. Retaining the
whole renderer survived 16 cycles but is not a qualified production fix.
All diagnostic code edits were removed.

The reduced 2D-camera comparison survived 12 cycles, and the 3D camera with
`Environment3D.msaa_samples: 0` survived 16. Focus further investigation on the
3D HUD multisampled render/resolve path across renderer recreation. No default
MSAA setting or platform-specific code was changed.
