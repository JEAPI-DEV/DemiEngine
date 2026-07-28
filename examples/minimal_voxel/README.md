# Minimal Voxel — Phase 6 Rendering Probe

This example now uses the game-facing rendering system end to end:

- authored terrain and first-person materials;
- environment, directional sun, and a local point light;
- a primary perspective camera plus an orthographic minimap render target;
- native-resolution HUD over a scaled world pass and a cached 5 Hz minimap;
- per-camera post processing;
- deterministic weather and beacon particles with Android budgets;
- world-space text and a scripted day/night presentation cycle.

The scripts generate voxel data and animate authored component values. They do
not contain raylib calls or renderer-specific material logic.

The terrain art originates from the
[`oCd resource pack`](https://resourcepack.net/ocd-resource-pack/).

Import the terrain atlas from the checked-in pack files with:

```sh
lua5.4 examples/minimal_voxel/tools/import_pack.lua examples/minimal_voxel
```

The script uses `./build/linux-debug/demi` to register its generated atlas.
Pass another CLI path as the second argument or set `DEMI_CLI` when needed.
Hashing and manifest metadata are owned by the engine's asset pipeline.

This generates ignored runtime files under
`examples/minimal_voxel/assets/generated/` and
`examples/minimal_voxel/generated/`. If they are missing, the example still
runs with an untextured terrain mesh.
