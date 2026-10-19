# Demi Woodland Vegetation

Original CC0 low-poly starter art for Demi terrain. No downloaded meshes,
textures, external generators, or runtime scripts are required.

Add `demi.terrain.vegetation` version `^1.0.0` through the package workflow.
The palette is `asset://demi/vegetation/palettes/woodland`; it contains independent
grass, shoreline reed, oak, pine and boulder rules. Terrain-local units are
meters. Its physical densities are starting points; water, slope, spacing and
cluster filters reduce the resulting counts. The palette has no ground material
set so it can accompany an existing landscape appearance.

Reusable prefabs are `prefab://demi/vegetation/grass`, `reed`, `oak`, `pine` and
`boulder` under the same prefix. Grass and reeds have no colliders and use the
collision-none scatter path. Trees have only a trunk capsule, with independently
shaded bark and moving crown geometry. Boulders use a deliberately conservative
box collider; their visual silhouette is faceted stone. No leaf physics bodies
are created. Palette collision policies can disable the selective colliders.

All geometry uses indexed glTF triangles with authored normals and embedded
buffers. Model IDs use `asset://demi/vegetation/models/` followed by `grass`,
`reed`, `trunk`, `oak_crown`, `pine_crown` or `boulder`. Every model also has
`_medium` and `_low` alternatives with fewer blades, radial segments or crown
facets. Prefabs select these through standard MeshRenderer LOD fields and use
finite culling distances. Grass is approximately 0.65 m tall, reeds 1.8 m, trees
5.5 m, and boulders 2.2 m wide before placement scale variation.

Ordinary Material assets under `asset://demi/vegetation/materials/` supply bark,
leaves, grass, reed, stone and ground colors. Grass, reed and leaves opt into
shared height-weighted wind; bark and stone remain still. Wind is visual only
and does not affect collision or deterministic placement. Opaque double-sided
grass geometry avoids alpha sorting and texture dependencies.

Recreate the meshes with Python 3:

```sh
python3 packages/sources/demi.terrain.vegetation/tools/make_models.py
python3 packages/sources/demi.terrain.vegetation/tools/check_models.py
```

Run from any directory using the script's absolute path. The source algorithm
is deterministic and uses only Python's standard library. It writes maintained
source assets, not cooked/build output. After editing/rebuilding source meshes,
reimport the manifests through Demi before cooking a project. The qualification
project is `examples/vegetation_3d`.

All package content, including the generator, was created for DemiEngine and is
dedicated to the public domain under CC0-1.0; see LICENSE. There are no external
art dependencies and no attribution requirement.
