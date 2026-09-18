# Kenney Prototype Textures

78 ready-to-use PNG textures: 13 textures in each of Dark, Green, Light,
Orange, Purple and Red. Original artwork by Kenney, distributed under CC0.
Demi packaging does not imply endorsement by Kenney.

## Install

For the engine repository's local registry:

```sh
demi package add kenney.textures.prototype@1.0.0 --registry /absolute/path/to/DemiEngine/packages
```

Once published to demiengine.de, omit `--registry`. No manual extraction,
asset imports, Lua module or startup preload list is needed. Installed package
assets are registered automatically; scene references load the textures they use.

## Use

Select a texture in the Mesh Renderer's Texture field, or reference its stable ID:

```json
"MeshRenderer": {
  "shape": "cube",
  "size": [2, 2, 2],
  "texture": "asset://kenney/prototype/orange/texture_01"
}
```

IDs follow `asset://kenney/prototype/<color>/texture_<01..13>`.
Colors are lowercase: `dark`, `green`, `light`, `orange`, `purple`, `red`.
Numbering matches the original archive; the same number is not necessarily
the same pattern across colors. See Preview.png and Sample.png for the artwork.
Textures also work in 2D wherever a Texture2D asset is accepted.

Manifests request repeating, mipmapped trilinear sampling. Geometry still needs
UV coordinates; this pack does not automatically unwrap imported models.
These are color textures, not PBR normal/height/material sets.

## License and contents

See the unmodified License.txt from Kenney's Prototype Textures 1.0 (2020-04-08).
Attribution is appreciated but not required. The original ZIP is left untouched.
Only PNG textures and original previews are shipped; Flash/SVG source variants
and URL shortcuts are omitted. There are no generated mesh assets or custom
runtime scripts.

Engine maintainers can verify the package with
`python3 scripts/test_prototype_texture_package.py` from the engine root.
Store listing metadata (asset type, CC0 license and original preview images)
is in `tools/package-store/catalog/kenney.textures.prototype.json`.
