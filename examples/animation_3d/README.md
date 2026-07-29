# 3D Animation

This example plays skeletal animations embedded in a standard glTF/GLB model
through the shared animation state machine. Use the buttons on the left to
switch between the model's named idle, walk, sprint, jump, and dance clips.

`AnimationPlayer3D` belongs beside a `MeshRenderer` that references a `Model3D` asset:

```json
"AnimationPlayer3D": {
  "clip_name": "Idle_Loop",
  "speed": 1.0,
  "loop": true,
  "playing": true
}
```

Embedded glTF animation names can be used directly. In this asset, `clip_0` is
`A_TPose`, so selecting it appears static by design. The example now starts
with `Idle_Loop` and exposes several visibly animated clips. Every imported
clip also has a deterministic `clip_N` alias, so unnamed clips remain
addressable without depending on renderer internals. `AnimationStateMachine`
states use `model_clip_name` and share the same transitions, parameters, and
Lua controls as 2D animation states.

The project declares a 16.67 ms frame budget, 64 draw calls, and 32 resident
assets. These numbers are reference-scene limits, not engine-wide defaults.
