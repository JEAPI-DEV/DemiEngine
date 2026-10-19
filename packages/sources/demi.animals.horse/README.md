# Animated horse

A textured bay horse with a 19-joint skeleton and looping Idle, Walk and Run
animations. CC0; no dependencies or Lua module installation required.

Version: 0.1.0. DemiEngine 0.1.x.

Install from this checkout's local registry:

```sh
demi package add demi.animals.horse@0.1.0 --project <project> --registry /path/to/DemiEngine/packages
```

Use `prefab://demi/animals/horse` for a visual horse with Transform3D,
MeshRenderer, AnimationPlayer3D, and AnimationStateMachine. The model alone is
`asset://demi/animals/horse`. Both references remain stable across projects.

The prefab starts in `idle`; available states are `idle`, `walk`, and `run`,
playing clips `Idle` (3 seconds), `Walk` (1.2 seconds), and `Run` (2/3 second).
Run is a running trot. Clips animate in place: gameplay supplies movement,
authority, collisions, mounting, and rider behavior. The visual prefab has no
collider or movement scripts.

Coordinates use metres, +Y up and +Z forward, with feet at Y=0. Hip height is
approximately 1.6 metres. A starting rider seat is [0, 1.67, 0.42] in model space;
adjust it for the rider's proportions. One horse uses 14,986 triangles.

The GLB embeds a single opaque albedo atlas shared by all mesh primitives.
Keep MeshRenderer's `material` omitted to use the built-in skinned material and
embedded texture. Mane and tail use opaque dark geometry for this rendering
path. The package contains the runtime GLB and manifests; editable original
sources and the reproduction script remain in the DemiEngine checkout at
`examples/amber_pass_3d/assets/horse/` and `tools/demo-art/amber_pass/horse.py`.

See CREDITS.txt for original source links, authors, and modifications.
