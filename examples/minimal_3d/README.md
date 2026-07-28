#Minimal 3D Example

This reference scene exercises hierarchical transforms, glTF materials,
texture importer settings, the public capsule character controller, a
kinematic moving platform, trigger pickups, continuous rigidbody projectiles,
authored gameplay colliders, and debug colliders. Its
project file sets explicit budgets of 16.67 ms per frame, 128 draw calls, and
64 resident assets.

The background track is a regular `AudioClip` asset played by a looping
`AudioSource`, so music configuration remains in scene data.

Used assets:

- [`https://sketchfab.com/3d-models/hyena-realistic-3d-model-demo-free-c1edd7dcb2f1478990858c1fccad8efe`](https://sketchfab.com/3d-models/hyena-realistic-3d-model-demo-free-c1edd7dcb2f1478990858c1fccad8efe)

The rendered hyena intentionally uses a simple authored box collider. A dense
triangle mesh is useful for immovable level geometry, but is a poor character
gameplay collider because every controller sweep must traverse its triangles.
