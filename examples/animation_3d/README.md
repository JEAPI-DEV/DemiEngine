# 3D Animation

This example plays the first skeletal animation embedded in a standard glTF/GLB model.

`AnimationPlayer3D` belongs beside a `MeshRenderer` that references a `Model3D` asset:

```json
"AnimationPlayer3D": {
  "clip": 0,
  "speed": 1.0,
  "loop": true,
  "playing": true
}
```

`clip` is the zero-based animation order from the GLB. glTF clips are sampled at the importer’s native 60 Hz timeline; `speed` scales playback without changing source timing.
