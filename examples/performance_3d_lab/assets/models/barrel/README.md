# Steel barrel stress asset

Original asset authored in Blender for this lab; no downloaded model or textures.

- `barrel.blend`: editable barrel and a separate preview camera/light setup.
- `barrel.glb`: one static mesh, two materials, 2,676 triangles, no textures or
  animations. Rolled rims, two reinforcing ribs, inset lids, and two fill caps.
- `barrel.asset.json`: normal `Model3D` import manifest.
- `assets/colliders/barrel/barrel.collider.json` (project-relative): reusable
  convex geometry. The optional project-level `prefabs/barrel.prefab.json`
  references it, but scripts do not need that prefab.

The exported model is centered, Y-up, 0.70 m wide/deep and 0.95 m tall. The
collider file defines a 32-point, 16-sided convex cylinder of the same outer dimensions.
It approximates the outer barrel envelope, not the small rib recesses or bung
details; it is neither a sphere nor a box. MeshRenderer scale stays at one.

To re-export, select only `Demi_Steel_Barrel` in the `Demi_Barrel_Asset` scene.
Use GLB, Selected Objects, Active Scene, +Y up, Apply Modifiers, normals and
tangents, and no animation. Then run from the repository root:

```sh
./build/linux-release/demi asset reimport examples/performance_3d_lab/assets/models/barrel/barrel.asset.json
./build/linux-release/demi validate examples/performance_3d_lab
```

If dimensions change, update/reimport the authored collider file and its upright/side
rest-height test too. Blender's preview lighting is not the engine's rendering
setup; use the running demo to review the actual engine appearance.

The stress sweep renders the full mesh at every distance, with no LOD swaps or
distance culling. At 5,000 instances that is 13.38 million barrel triangles before
the floor. This is intentionally a different rendering and collision workload
from the sphere baseline.
