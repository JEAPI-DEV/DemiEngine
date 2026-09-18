# Streamed brick destruction range

```sh
demi package install --project examples/destruction_weapons_3d_lab
demi run --project examples/destruction_weapons_3d_lab
```

Click to capture the mouse; Tab releases it. WASD moves, LMB swings the hammer,
RMB fires, F refills, and R reloads. The HUD reports applied/failed impacts.

## Compact source, runtime geometry

`prefabs/doorway.prefab.json` contains three compact `Masonry3D` regions,
a steel lintel, door and hinges—not hundreds of manually authored bricks.
Its 2.4 m high, 115 mm thick masonry uses 1800 kg/m³. Ordinary cells weigh about
6.47 kg and header cells 5.18 kg. The hollow door weighs 31.92 kg.

The supplied Bricks085 color and displacement maps stay under `assets/Bricks`.
Two small texture manifests reference those original files without duplicating
them. The renderer generates relief meshes in memory from the height map, with
up to 12 mm recess and smooth normals. It shares/instances the resulting variants
across active walls. No generated GLB or Blender wall assets are required.

Collision remains a simple per-cell box. Cuts are rectangular masonry tiles,
not a reconstruction of every irregular mortar joint in the photograph.
Procedural relief is not a substitute for the engine's still-incomplete lighting.

## Streaming, cleanup and saving

The `world_stream` entity contains two compact placement records. Its script
uses the reusable destruction streaming package. Walls activate within 24 m and
unload beyond 32 m; damage survives that round trip in memory. At most eight
assemblies are active and one is spawned per update. Shared source assets stay
resident; inactive walls have no generated entities or native bodies.

The lab opts into retiring loose debris after 45 idle active seconds. Holes stay
missing. Set `debris_lifetime=0` on World Stream to disable time-based retirement.
Cleanup is whole-assembly policy, not an important-prop exemption system.

Disk saving is disabled by default. Set World Stream's `save_slot` property and
press SAVE to persist damage through the engine's user-data save service. The
next scene load reads that slot. Saves contain damage and physical state, not
expanded wall geometry. Queued impacts must finish before saving/unloading.

## Verification and limits

The floor and rear backstop use `kenney.textures.prototype` from the local
package registry. Run `demi package install --project examples/destruction_weapons_3d_lab`
after pulling the example. Their surface UVs repeat the grid every two meters;
the static collision boxes are unchanged. Textures load through normal scene
references, not a manual preload of the entire pack.

`Environment3D.sky_texture` uses the tone-mapped JPEG from your EveningSky ZIP.
It is a camera-centered, depth-safe panorama in both editor and game. The EXR
and original ZIP remain untouched; HDR/IBL/reflections are not implemented by
this background pass.

`demi test linux --project examples/destruction_weapons_3d_lab` checks weapon
timing, native completion, rocket travel, hinge release, debris cleanup,
distance unloading, retained holes on return and independent pristine instances.
Native tests cover checkpoint rejection, mass/momentum and transactional restore.
Package tests exercise a 100,000-record catalogue with bounded scans/spawns;
this is not a 100,000-wall graphics/physics benchmark.

Generation is proximity-triggered and first-use preparation is synchronous.
Intact visual compaction, worker/time-budgeted preparation, distant proxies,
material stress, recursive fracture and platform performance qualification remain
open. See [streamed destruction](../../docs/streamed-destruction.md).

Obsolete exported masonry files and their bake script were moved out of developer
asset folders to `build/destruction-generated-masonry-v2` at the repository root,
where they remain recoverable. Earlier unused prototypes are in
`build/destruction-brick-relief-unused`. Your downloaded texture pack was untouched.
