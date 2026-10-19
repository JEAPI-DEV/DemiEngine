# Amber Pass

A playable mounted traveller on an autumn road through a rocky valley and a
broken chapel gate. This is a Linux lightweight 3D example: engine-native
terrain, imported environment meshes, shared PBR materials, skeletal horse
clips, a capsule character controller, and a chase camera that avoids solid
geometry.

The continuous native terrain spans 720 × 720 metres. Native mountain noise
rises outside a protected road corridor; sculpted hills shape the playable
valley. Eighty trees and tens of thousands of grass instances surround the
road, with authored model LODs and a grass cull distance.

The application starts in a small loading scene. Its progress bar follows CPU
scene and collider preparation, staged resource work, and presentation readiness.
It enters the valley only after preparation succeeds.

From the repository root, build the optimized desktop runtime and editor:

```sh
cmake --preset linux-release
cmake --build --preset linux-release
./build/linux-release/demi package install --project examples/amber_pass_3d
./build/linux-release/demi validate examples/amber_pass_3d
./build/linux-release/demi run --project examples/amber_pass_3d
```

To inspect or author the scene:

```sh
./build/linux-release/demi editor --project examples/amber_pass_3d
# Open the reusable mounted hierarchy directly:
./build/linux-release/demi-editor --project examples/amber_pass_3d --open prefabs/amber_pass/mounted_traveller.prefab.json
```

W rides forward; S brakes and reverses slowly. A/D steer, including while
stationary. Releasing movement uses a separate braking rate, so the horse stops
promptly instead of coasting at its acceleration rate. Hold Left Shift to move
faster. Hold the right mouse button and drag to orbit the camera; moving without it lets the camera follow behind.
R returns the mount to its starting position. Enable **Touch controls** to show
the HUD stick. It feeds the same movement action through touch or pointer
dragging; toggle it again to hide the stick.

Movement is resolved by `CharacterController3D` against terrain and authored
static colliders. Imported animation remains in place: completed capsule
movement selects `idle`, `walk`, or `run` on the horse's
`AnimationStateMachine`, which drives `AnimationPlayer3D`. The fast clip is a
running trot. The rider and tack are static meshes attached to the mount;
this example does not implement rider IK, dynamic reins, horse simulation,
combat, quests, or large-world streaming. Android qualification is not included.

The project E2E test enables and drags the actual HUD stick, then checks
movement, grounding, braking, and camera placement. Run it through the real
runtime:

```sh
./build/linux-release/demi test linux --project examples/amber_pass_3d
```

A short headless smoke checks scene/script loading, without qualifying visible
rendering or GPU asset preparation:

```sh
DEMI_HEADLESS=1 ./build/linux-release/demi run --project examples/amber_pass_3d --max-frames 6
```

Art preparation sources live in `tools/demo-art/amber_pass/`. The horse derives
from the CC0 Realtime Ranchers mesh by Lyndon Daniels and the armature by ChadM;
[the horse package credits](../../packages/sources/demi.animals.horse/CREDITS.txt)
record source URLs, author credits, atlas details, and clip durations. Rider,
saddle, and chapel geometry are original assets authored for this example.
The environment uses Poly Haven assets; [download records](assets/environment/downloads.json)
retain official URLs and hashes, and [the asset summary](assets/environment/summary.json)
records exported meshes and material sources. Terrain materials also use the
local `polyhaven.materials.terrain` package.

Rebuilding art requires Blender with its glTF importer/exporter and bundled
NumPy. Run these from the repository root in the listed order. Environment
preparation restores missing downloads into `/tmp/demi-environment` and verifies
their recorded hashes. Horse preparation reads the preserved Blender source.

```sh
blender -b --python tools/demo-art/amber_pass/horse.py
blender -b --python tools/demo-art/amber_pass/rider.py
blender -b --python tools/demo-art/amber_pass/ruins.py
blender -b --python tools/demo-art/amber_pass/environment_assets.py
python3 tools/demo-art/amber_pass/scene.py
```

These are source authoring commands: they rebuild the example's art, manifests,
scene, prefab, project, and HUD. `scene.py` uses `build/linux-release/demi` to
install packages, reimport manifests, and create missing static collider assets.
Its `terrain_landscape.py` module owns the native mountain recipe, road mask,
terrain component settings, and the corridor height sampler used for prop
placement. Mountain noise stays in the engine; the Python sampler matches the
native grid triangles within the protected corridor.
Back up intentional edits before regenerating authored files. Rider and ruins
retain editable Blender files beside their GLBs; temporary previews are written
below `/tmp`. Environment preparation also rebuilds foliage and grass clusters. Cooking and packaging remain owned by the engine CLI.

Grass retains 30,637 authored clumps. Full geometry is used near the camera,
with medium/low models beyond 10/35 metres and a 160-metre visibility limit.
These distances are editable Mesh Renderer settings, not engine limits.

See [loading and performance qualification](../../docs/amber-pass-loading-performance.md)
for measured costs, regression checks, and remaining rendering limitations.
