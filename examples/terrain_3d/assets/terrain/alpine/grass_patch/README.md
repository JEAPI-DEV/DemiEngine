# Grass source opacity

The original clustered model used an RGB diffuse image with a MASK material.
The missing opacity made the atlas background render as opaque black triangles.
The original Poly Haven `grass_medium_01` opacity map is now retained here; its
URL, retrieval date and verified source checksum are in `SOURCES.json`.

`grass_medium_01_diff_2k-grass_medium_01_alpha_2k.png` is the packed RGBA albedo
referenced by the glTF. The original RGB diffuse and separate opacity map remain
source inputs. Geometry was preserved (7,830 triangles; positions agree with the
previous source to 1e-6 metres).

Reproduce with separate background Blender processes:

```sh
blender --background --factory-startup --python scripts/art/restore_gltf_opacity.py -- \
  --source examples/terrain_3d/assets/terrain/alpine/grass_patch/grass_patch.gltf \
  --opacity examples/terrain_3d/assets/terrain/alpine/grass_patch/grass_medium_01_alpha_2k.png \
  --material grass_medium_01 --cutoff 0.35 --output /tmp/grass-repaired/grass_patch.gltf
blender --background --factory-startup --python scripts/art/bake_albedo_impostor.py -- \
  --source /tmp/grass-repaired/grass_patch.gltf --output /tmp/grass_card.png \
  --center-height 0.36 --width 1.8 --resolution 1024
```

Review/adopt the declared glTF resources as source files, then use native asset
reimport. Do not edit generated/cooked output. The distant card geometry retains
its existing UV framing and six triangles. See `LICENSE.txt` for the CC0 license.

The intermediate source lives in `../grass_mid/`. It retains the clump's
placement frame and byte-identical texture maps, with 1,566 triangles from a
Blender 5.2.2 Decimate collapse at ratio 0.2 (`use_collapse_triangulate = True`).
Export as glTF Separate and retain MASK cutoff 0.35 and double-sided materials.
The native error-bounded LOD generator retained 7,766 triangles on this leaf-card
mesh because of seams; that result was not used as the intermediate LOD.
The prefab uses the intermediate mesh from 14 to 45 metres, with distant cards
beyond 45 metres. Geometry/model LOD switches still have no temporal crossfade.
