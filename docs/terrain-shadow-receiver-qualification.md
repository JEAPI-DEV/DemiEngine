# Terrain shadow receiver qualification

Initial investigation: 2026-10-13. Build: `linux-release`. Native backend: Vulkan through SDL/X11.

The follow-up below resolves the prominent rocky-valley self-shadow grid.
The initial investigation is retained because its numerical fix alone was
insufficient.

## Initial numerical correction

The shared shadow shader previously accepted a UV Jacobian only when its
absolute determinant exceeded `1e-12`. That rejects valid small projections:
`J = diag(1e-7, 1e-7)` with depth derivatives `(2.5e-8, 5e-8)` has the exact
UV depth gradient `(0.25, 0.5)`, but the old shader substituted zero.

`demi_shadow_receiver.sh` now owns the receiver-plane solve. Each UV row and its
corresponding depth derivative are normalized together; rejection uses relative
projected area. Zero and nearly parallel rows retain the safe zero-correction
fallback. Authored bias, filtering, shadow distance and terrain geometry are
unchanged. Both ordinary materials and GPU terrain use this shared calculation.
The helper is an explicit dependency of both shader build rules.

The analytical basis remains planar receiver correction, as described by
[Microsoft's cascaded shadow mapping documentation](https://learn.microsoft.com/en-us/windows/desktop/DxTechArts/cascaded-shadow-maps).
Correct plane math does not establish that neighboring terrain facets belong
to the same plane or explain every visible shadow artifact.

## Tests and native checks

`demi-shadow-receiver-gradient-tests` includes the actual production shader
helper through a small test-only vector arithmetic adapter. Release assertions
are enabled. Tests cover the rejected small Jacobian, independent row scales,
atlas V inversion, singular/nearly parallel rows, corrected PCF taps on a plane
and a distinct closer occluder. It passes alongside shadow-cascade-layout and
bgfx-renderer3d tests (3/3). Shared shaders compile for configured SPIR-V, GLSL
and ESSL variants; the Release runtime/editor were rebuilt.

`scripts/generate_shadow_receiver_probe.py` creates a two-triangle inclined
plane and a box without launching an engine. Scale-1 and scale-0.01 projects
validate. Captures use the same 1600-by-900 window and scale camera, clipping,
shadow range and bias with the geometry. This checks scale stability; uniform
world scaling does not itself reproduce a tiny normalized UV Jacobian.

Before/after planar captures were pixel-identical. The box shadow remains:
RGB at `(736,540)` was `(46,49,56)` with shadows and `(73,75,78)` without them.
A clear receiver pixel at `(950,250)` remained `(73,75,78)`. Both coordinates
matched at scale 0.01. A clean planar probe does not qualify the rocky valley.

Native runs were sequential, with one instance active at a time. Temporary
captures: `/tmp/shadow-plane-{before,after,small,off}.png`. No performance
improvement is claimed for this correctness change.

## Historical remaining cliff artifact

The unchanged rocky valley was captured at 2954-by-1744. Striping is still
visible after the conditioning fix. It also appears with GPU terrain disabled,
so it is not isolated to height-texture displacement or material interpolation.
Hard filtering changes the grid into sharper patches/speckles but does not
remove the artifact. Temporary isolation projects changed only the filter or
GPU cache setting; repository scene settings were preserved.

Captures: `/tmp/shadow-valley-conditioned.png`, `/tmp/shadow-valley-hard.png`,
`/tmp/shadow-valley-mesh.png`. The next investigation should distinguish actual
neighboring-facet occlusion from shadow-filter/projection error on a controlled
non-planar receiver. No normal offset or arbitrary bias increase was introduced.
The terrain shadow-quality and broader terrain milestones remain open. There
was no website deployment or physical Android rendering qualification.


The user deferred further shadow investigation on 2026-10-13 in favor of the
remaining terrain features. Keep this issue recorded; it does not block the
polygon-mask and macro-landform work.

## Follow-up: translated views and filtered facets, 2026-10-14

Release runtime/editor rebuilt; native checks use Vulkan through SDL/X11.
The rocky-valley recipe, 500-by-500 grid, 700-unit shadow distance, 2048-pixel
cascade resolution and 0.025 authored bias are unchanged. There is no
platform-specific workaround or new authoring setting.

Three separate corrections were needed:

1. The renderer formed a float target as `eye + forward`, then the backend
   recovered direction by subtracting eye. At valley coordinates this rounded
   the unit direction, while receiver projection still used the original light
   direction. `View3DConfig` now carries forward directly. The backend builds
   orientation at the origin, then computes translation from those axes. The
   same fix applies to ordinary cameras and shadow cameras.
2. Fragment derivatives of interpolated absolute world positions lost small
   pixel differences on translated receivers. Builtin ordinary, instanced,
   skinned and terrain vertex shaders now also interpolate a view-relative
   position, used only for receiver-plane derivatives. Subtraction happens
   before interpolation. World-space lighting and authored geometry remain
   unchanged.
3. A receiver plane extrapolated across a neighboring facet can falsely shadow
   that facet even with exact plane math. Receivers now use a normal offset
   bounded by the cascade's world-space texel size and nominal filter radius:
   0.5 texels for hard sampling, 1.5 for 3x3 PCF, multiplied by the sine of the
   normal/light angle. The shading-normal offset is held constant across each
   receiver's taps, so the original gradient describes its parallel translated
   plane. Authored depth bias is still applied separately.

The last correction is a finite-filter tradeoff, not exact ray visibility.
Contacts within the offset can soften or detach, particularly in coarse distant
cascades. More shadow resolution or tighter coverage reduces that world-space
footprint. It does not require increasing a scene-wide bias to one metre.

### Reproduction and measured image checks

The checked-in probe generator now accepts `--origin`:

```sh
python3 scripts/generate_shadow_receiver_probe.py /tmp/plane-origin
python3 scripts/generate_shadow_receiver_probe.py /tmp/plane-translated --origin 1000 350 1850
python3 scripts/generate_shadow_receiver_probe.py /tmp/plane-small --scale 0.01
```

Before the fix, translating the complete plane/box/camera scene made the plane
self-shadow, while the origin scene remained clean. Correcting view orientation
restored the box shadow but left fine noise; relative derivatives removed that
noise. The final translated and scale-0.01 probes retain the localized box shadow
and a clear receiver. At pixel (2000,500) in matching 3413-by-1917 translated
captures, the false-shadow RGB `(46,49,56)` becomes the expected `(73,75,78)`.

The unchanged rocky valley was checked at wide and close views, with both GPU
terrain and ordinary-mesh fallback. The prominent grid is removed with PCF;
hard filtering also improves but retains sharper finite-resolution edges.
The broad valley cast shadow remains visible. Material/geometry faceting and
small sharp-crease residuals are not claimed to disappear.

For a directly lit left-cliff region `x=200..649, y=650..999` in matched full-size
captures, mean absolute RGB difference from the shadows-off control decreased
from 34.33377 to 0.00047 on the 0–255 channel scale. Pixels darker than the control
by more than 15 summed RGB levels decreased from 156,239 to 2 of 157,500. This is
one known unoccluded region, not a claim that shadows should match a disabled
control across the whole image. The distinct box and valley shadows verify that
the correction did not simply disable shadowing.

Temporary captures: `/tmp/shadow-current.png` (before),
`/tmp/shadow-filter.png` (final wide), `/tmp/shadow-close-final.png`,
`/tmp/shadow-mesh-final.png`, `/tmp/shadow-hard-final.png`,
`/tmp/shadow-shifted-before.png`, `/tmp/shadow-shifted-filter.png` and
`/tmp/shadow-small-final.png`. All native launches were sequential, with no other
engine instance or build running during capture. No FPS improvement is claimed.

### Regression coverage

All four focused suites pass. They cover production view-matrix orientation under
translations through 1e8 units (orientation only, not an arbitrary-large-world
precision guarantee), actual cascade uniform texel size at 512/1024/2048,
receiver gradients, and actual shader-helper arithmetic on adjacent facets.
The facet test first proves the unoffset false shadow, then checks the corrected
comparison and a distinct closer occluder beyond the offset. It covers mirrored
slopes, hard/PCF radii and uniform scales. SPIR-V, GLSL and ESSL shader variants
compile. Rocky-valley and shadows example sources validate, and the three-frame
headless shadows example passes.

These are shared backend/shader changes. Native Windows and physical Android
rendering were not qualified; no website deployment is claimed.
