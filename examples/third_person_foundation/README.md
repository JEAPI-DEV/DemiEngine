# Third-person foundation room

A small playable mechanics probe requiring no downloaded models or Blender.
Copy this directory to begin experimenting. The fighter prefab owns collision,
visual and weapon children; main.scene.json owns room geometry and instances.
room.lua coordinates the package and explicitly processes defence before damage.

From the engine root:

```sh
./build/linux-debug/demi package install --project examples/third_person_foundation --locked --offline
./build/linux-debug/demi run --project examples/third_person_foundation
```

WASD moves relative to the camera. Mouse orbits, Tab locks onto the enemy,
F/LMB attacks, Shift/Space rolls, Escape releases/captures the cursor,
R resets. The raised hand telegraphs an attack; forward motion is its active
window. Recovery leaves time to counterattack. The HUD displays phase, health
and stamina. Walk against the walls/pillar to inspect camera obstruction.

Tune player settings and the two attack definitions in room.lua. Both fighters
use the same prefab and melee module. Replace the visual child with an imported
model when the mechanics are ready; this room does not claim polished character
animation or production-grade hit volumes.
