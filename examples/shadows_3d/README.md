# Directional shadows

Run `demi run --project examples/shadows_3d` from the repository root.
The suspended cube moves and rotates; its shadow should follow it each frame.

Geometry edges use the engine's default 4× MSAA. Set `Environment3D.msaa_samples`
to `0` to compare it with unfiltered edges, or choose `2`, `8`, or `16`.

`DirectionalLight.casts_shadows` enables the pass. `Environment3D` controls
`shadow_resolution`, `shadow_distance` (coverage radius in metres), and
`shadow_bias` (metres). Zero distance or `max_shadow_lights: 0` disables it.

This example exercises one directional map with filtered opaque/cutout shadows.
It does not demonstrate cascades, point-light shadows, or transparent shadows.
