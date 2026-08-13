# 2D Physics Galton Board

A Galton board demonstrating dynamic circle bodies, static circle and rotated
box colliders, restitution, friction, continuous collision, and deterministic
fixed-step simulation.

Press `Space` to release 60 balls from the funnel. Repeated left/right
collisions with the staggered pegs produce an approximately normal
distribution in the bins. Press `R` to reset the board.

```sh
./build/linux-debug/demi run \
  --project examples/physics_2d_galton_board/demi.project.json
```
