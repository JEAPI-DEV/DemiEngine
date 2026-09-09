# 3D Physics Galton Board

A classic upright Galton board modeled as a genuine 3D cabinet and used as a
repeatable Jolt rigid-body performance probe. It has a thick wooden backboard,
projecting frame and rails, transparent front glass, and silver cylinder pegs
that span the cabinet depth. The machine releases 250 physical 3D spheres.
All cabinet geometry is authored directly in `scenes/main.scene.json`, so its
transforms, renderers, materials, and colliders can be adjusted in the editor.
Lua owns only runtime ball spawning and simulation statistics.

The camera faces the board straight on like the physical reference, while
lighting, the visible peg shafts, glass, frame, and tabletop reveal its depth.
No ball axis is locked and balls are never teleported into bins; the backboard
and front glass physically contain their depth motion.

Press `Space` to begin and `R` to reset. Balls are released after the previous
one clears the hopper throat, avoiding a spawn jam while preserving all later
ball-to-ball, ball-to-peg, and ball-to-bin collisions. A seeded horizontal
perturbation on each peg impact represents microscopic launch differences;
the replay verifies the resulting bell-shaped bin distribution.

```sh
./build/linux-debug/demi run \
  --project examples/physics_3d_galton_board/demi.project.json
```

To capture subsystem timings:

```sh
DEMI_HEADLESS=1 ./build/linux-debug/demi run \
  --project examples/physics_3d_galton_board/demi.project.json \
  --input-replay examples/physics_3d_galton_board/replays/release.replay.json \
  --max-frames 4800 --profile-report /tmp/physics_3d_galton_board.csv
```
