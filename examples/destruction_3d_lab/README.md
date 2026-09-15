# 3D Destruction Lab — compound foundation

```sh
demi run --project examples/destruction_3d_lab
```

This first Milestone 3 probe verifies a real compound collider: the arch is one
dynamic body with three convex parts, not three independently simulated child
bodies or a solid bounding box. Click a part to inspect its stable raycast ID,
press Space (or Impulse) to move the whole assembly, and R (or Reset) to restart.
Each scene click reports its hit or miss both in the status label and the console.

The entire scene is authored in `scenes/main.scene.json`. The renderer-only
children follow `arch`; collision geometry and local part IDs live in
`assets/colliders/arch/arch.collider.json`. Reimport its manifest after editing
the collider file. The Lua script handles input only, not scene construction.

This is not the completed destruction demo: Blast damage, fracture bonds,
hammer/rocket interactions, split-body transactions and the steel door remain
Milestone 3 work. The primitive arch is an engine probe, not final art.
