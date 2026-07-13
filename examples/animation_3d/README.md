# 3D Animation

This example plays a skeletal animation embedded in a standard glTF/GLB model
through the shared animation state machine.

`AnimationPlayer3D` belongs beside a `MeshRenderer` that references a `Model3D` asset:

```json
"AnimationPlayer3D": {
  "clip_name": "clip_0",
  "speed": 1.0,
  "loop": true,
  "playing": true
}
```

Embedded glTF animation names can be used directly. Every imported clip also
has a deterministic `clip_N` alias, so unnamed clips remain addressable without
depending on renderer internals. `AnimationStateMachine` states use
`model_clip_name` and share the same transitions, parameters, and Lua controls
as 2D animation states.

The project declares a 16.67 ms frame budget, 64 draw calls, and 32 resident
assets. These numbers are reference-scene limits, not engine-wide defaults.
