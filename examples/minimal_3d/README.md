#Minimal 3D Example

This reference scene exercises hierarchical transforms, glTF materials,
texture importer settings, collision-aware movement, CLI-generated glTF
collider assets, debug colliders, and the `Physics3D` spatial-query API. Its
project file sets explicit budgets of 16.67 ms per frame, 128 draw calls, and
64 resident assets.

The background track is a regular `AudioClip` asset played by a looping
`AudioSource`, so music configuration remains in scene data.

Used assets:

- [`https://sketchfab.com/3d-models/hyena-realistic-3d-model-demo-free-c1edd7dcb2f1478990858c1fccad8efe`](https://sketchfab.com/3d-models/hyena-realistic-3d-model-demo-free-c1edd7dcb2f1478990858c1fccad8efe)

The hyena's collider was generated without hand-authored dimensions:

```sh
demi asset collider assets/models/hyena/hyena.asset.json \
  --project . --id asset://colliders/hyena --detail 1
```

Use `--detail 0` for a fast bounding box; values between `0` and `1` retain a
deterministic subset of the hyena mesh triangles.
