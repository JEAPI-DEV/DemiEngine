# Silvermere terrain showcase

The current startup scene is **Alpine region**, an experimental 240 × 220 km
procedural streaming landscape. Open `scenes/alpine.scene.json` to edit it.
The region wrapper lives in `assets/terrain/regions/alpine/alpine.json`; its
referenced Terrain asset owns the graph, materials, biome rules and scatter
palette. Select the terrain and choose **Open regional Terrain graph**.

The editor starts at the authored flight camera and streams nearby vegetation.
Hold RMB to fly with WASD/QE, scroll while holding RMB to change fly speed, and
hold Shift for a temporary boost. Frame Scene gives a whole-region overview.
In Play, use WASD to fly, Q/E for altitude, RMB or arrows to look, Shift for
speed, R to reset, and Escape to quit. The camera is a free-flight inspection
camera and can pass through the terrain.

Regional generation currently requires a source project. Cooked regional
programs, floating-origin gameplay and regional erosion/hydrology remain
unqualified. This example does not imply production-scale 3D support.

The opening view stays behind a preparation message until its terrain cover is
ready, instead of showing the coarse-to-fine construction sequence. Regional LOD changes during camera movement now morph geometry over 0.25
seconds, retaining the finer grid through coarsening. Collision keeps its
prepared snapshots; material and vegetation LOD appearance are separate. Grass draws to
700 m (768 m residency), while trees/rocks stream within 3,072 m. The tree's
near/mid geometry and six-triangle distant impostor share the same source art.
These distances are configured in the region asset and vegetation prefabs;
long-range canopy does not generate dense grass throughout its entire radius.

## Finite Silvermere tour

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
[-384, 0, -384]; graph coordinates are terrain-local. The separate loading scene prepares this finite tour. The Alpine region is
the project's startup scene. Finite terrain cooked builds load prepared data.

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
