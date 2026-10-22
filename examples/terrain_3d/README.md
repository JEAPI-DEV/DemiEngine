# Silvermere terrain showcase

A 768 × 768 metre native terrain landscape with a lake, wooded meadows,
rocky ridges, a textured ruined lakeside cathedral and a continuous 70-second camera round trip.
The loading scene prepares the landscape before activating the tour.
The standalone demo opens fullscreen at the selected display's desktop resolution.

From the repository root:

```sh
demi package install --project examples/terrain_3d
demi run --project examples/terrain_3d
demi editor --project examples/terrain_3d
```

Space pauses/resumes, R restarts, N advances to the next chapter, and Escape
closes the standalone demo. The same
controls are available in the HUD. Seven named chapters make it possible to
revisit the same view when evaluating changes. Camera movement uses the reusable
`demi.camera.paths` package, with authored positions and aim points in
`scripts/terrain_route.lua`; chapter duration is an Inspector-visible script
property. This is runtime spline playback, not a native editor timeline.

In the editor, open `scenes/main.scene.json`, select **Silvermere landscape**,
and open Terrain Graph. Ridge noise, meadow/basin masks, water, biome rules and
scatter remain editable native terrain nodes. The scene places that terrain at
[-384, 0, -384]; graph coordinates are terrain-local. The separate loading scene
is the project's startup scene. Cooked builds load prepared terrain data.

The deterministic palette produces 149,125 placements: 120,825 grass patches,
4,525 reeds, 16,426 sorrel plants, 3,488 wildflower patches, 2,820 shrubs,
56 clustered rocks and 985 trees. These are batched visual placements, not
149,125 independently simulated entities. Scatter collision is disabled for this
camera showcase; native terrain collision remains available. Placement counts
are not counts of objects visible in every frame.

`demi.environment.woodland` provides textured CC0 tree, grass and moss-rock art
extracted from Amber Pass into a reusable package. `demi.terrain.vegetation`
provides reeds; Poly Haven terrain materials supply mapped ground surfaces.
Trees and rocks have no distance cutoff before the camera far plane (1800 m).
Grass uses full source geometry within 12 m, medium geometry to 38 m, then a
10-triangle baked coverage model through the camera range. The distant model
contains four side views and a top view of the actual source patch. Reeds
retain a 90 m cutoff; shrubs and flowering ground cover use 220 m.
Directional shadows cover 180 m. Only the showcase grass disables
`MeshRenderer.casts_shadows`; trees, reeds, sorrel, flowers, shrubs and rocks
retain casting. Grass still receives shadows and participates in camera/water
depth. Shadow distance does not remove the visible tree mesh.

Grass now has baked distant coverage, but LOD changes have no smooth fade.
Trees use authored reduced meshes, not impostors or automatically generated LODs.
Art composition and sustained native 4K60 remain unfinished; the final Linux
Release tour measured 21.39 ms median / 27.43 ms p95 frame work at 3840 × 2160.
These moving-tour measurements do not certify every view or device.
See [qualification and next work](../../docs/silvermere-showcase.md).

Run the real HUD/camera checks:

```sh
demi package test packages/sources/demi.camera.paths
demi test linux --project examples/terrain_3d --timeout 120
```

The test exercises automatic scene activation, moving/paused camera poses,
restart, chapter seeking and the final-to-first wrap through real HUD input.

The [3D showcase plan](../../3d-showcase-plan.md) inventories the supplied Flax
feature references, marks current support, and defines phased acceptance checks.
