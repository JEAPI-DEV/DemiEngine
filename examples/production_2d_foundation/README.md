# Production 2D Foundation

A small interactive probe for the Phase 4 gameplay APIs:

- kinematic capsule movement with `Rigidbody2D.move_and_slide`;
- capsule, polygon, tilemap, and trigger collision;
- mutable and animated tilemap layers;
- runtime navigation updates and path queries;
- tilemap object layers, physics overlap queries, and sprite ordering.

The goal trigger also exercises a game-authored shader. Its material references
`asset://shaders/goal_glow`, which is preloaded when the renderer starts. The
shader manifest selects GLSL 330 sources on Linux and GLSL ES 100 sources on
Android, so gameplay code never depends on the rendering backend.

Run it from the repository root:

```sh
./build/linux-debug/demi run \
  --project examples/production_2d_foundation/demi.project.json
```

Move with WASD or the arrow keys. Press Space to open or close the tile gate.
The HUD reports trigger phases and whether navigation can still reach the goal.
