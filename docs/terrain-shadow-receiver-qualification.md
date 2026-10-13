# Terrain shadow receiver qualification

Date: 2026-10-13. Build: `linux-release`. Native backend: Vulkan through SDL/X11.

## Numerical correction

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

## Remaining cliff artifact

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
