# 2D Physics Galton Board

A Galton board demonstrating dynamic circle bodies, static circle and rotated
box colliders, restitution, friction, continuous collision, and deterministic
fixed-step simulation.

Press `Space` to release 250 balls from the funnel. Balls are released one at a
time after the previous ball clears the throat, preventing an artificial
hopper jam while preserving ball-to-ball and ball-to-board collisions.
Repeated left/right collisions with the staggered pegs produce an approximately
normal distribution in the bins. Ball diameter is sized so the expected center
mass fits below the dividers instead of overflowing back into the peg field.
Each first peg contact applies a small seeded lateral perturbation representing
the microscopic contact differences that are below this scene's rigid-body
scale; the replay checks the resulting histogram against the 12-row binomial
expectation.
After crossing a bin entrance, a ball's physical outcome is final: its body is
counted once, but it remains a normal dynamic body and falls into the deeper
physical bin. The bins are sized to hold the expected center stack without
overflowing into the peg field. Press `R` to reset the board.

```sh
./build/linux-debug/demi run \
  --project examples/physics_2d_galton_board/demi.project.json
```
