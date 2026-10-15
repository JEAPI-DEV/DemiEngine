# Poly Haven Terrain Materials

Five real photo-based CC0 surfaces at 1K: Rock Face 03, Grass Ground,
Rocky Gravel, Sand 02 and Snow 02. Includes ordinary Demi Material assets,
a reusable surface-role set and a surface-only palette. No Lua or custom shader.
These are starter surfaces, not a claim that a scene is photorealistic.
Poly Haven and the original artists do not endorse DemiEngine.

## Install

Install from the package manager:

```sh
demi package add polyhaven.materials.terrain@1.0.0
```

For source development, add `--registry /path/to/DemiEngine/packages`.
Installed assets are discovered by the engine;
no copying into a project's assets directory or global startup preload is needed.

Use `asset://polyhaven/terrain/palettes/alpine` as a terrain recipe's `palette`.
The Material Set `asset://polyhaven/terrain/surfaces/alpine` binds ground/grass,
rock/cliff, sediment, sand, snow, wet_ground and underwater roles. It contains no
object placements. Add a project's tree/prop placement rules separately.

Individual materials use `asset://polyhaven/terrain/materials/<source-id>`.
They work on terrain and ordinary MeshRenderer geometry. All maps have repeating,
trilinear, mipmapped sampling. Set mesh and biome tint to white to see the source
albedo unmodified. Automatic snow can be disabled on warm biomes with
`allow_snow: false`; explicit biome Material assignments override role selection.

## Physical scale and interpretation

Demi's terrain-local unit is treated as one metre. Triplanar material tiling is
one repeat per scanned tile width, not a guessed visual scale:

| Source | Tile width | Tiling (repeats/metre) |
|---|---:|---:|
| Rock Face 03 | 2.7 m | 0.370370 |
| Grass Ground | 2.51 m | 0.398406 |
| Rocky Gravel | 1.81 m | 0.552486 |
| Sand 02 | 2.14 m | 0.467290 |
| Snow 02 | 2 m | 0.500000 |

The official API reports millimetres; raw measurements are retained in
SOURCES.json. The published dimensions have measurement uncertainty; extra
API decimal digits are not survey accuracy. Entity scaling changes this local
pattern's apparent world size. Materials use OpenGL (+Y) normals. Albedo is sRGB;
normal, roughness, AO and height channels are linear data. Diffuse/roughness/AO
use the provider's 1K JPEG delivery; normal/height retain lossless provider PNGs.
No texture was synthesized, resized or recolored during packaging.

Gentle macro variation modulates albedo over 50 terrain-local units to reduce
repetition. Its strength and scale are editable Material parameters.

Height maps are shading/layer-blend inputs; they do not alter terrain geometry
or collision. Wet Gravel reuses Rocky Gravel's albedo, normal, AO and height maps. It omits
the roughness map so its scalar roughness of 0.28 applies, and darkens ordinary
material base-color parameters. Maps replace their corresponding scalar;
keeping the dry roughness map would defeat the wet override.
It is an authored wet-look
variant, not a separate wet scan or fluid simulation. No metallic map is needed
for these dielectric surfaces (metallic is zero).

## License and provenance

All source artwork is CC0-1.0, verified at https://docs.polyhaven.com/en/faq.
Redistribution and commercial use are permitted; attribution is not required,
but is retained here. LICENSE.txt contains the standard CC0 legal text. This
pack is free; the original assets remain freely available from Poly Haven.
SOURCES.json records original URLs, artists, physical dimensions, download
hashes, byte counts and exact channel formats. Each original download was
checked against Poly Haven's published MD5 and additionally recorded with
SHA-256. Package manifests use Demi's filename-aware FNV source hashes.

Original artists: Dario Barresi and Rico Cilliers (Rock Face 03), Charlotte
Baglioni (Grass Ground and Sand 02), Dimitrios Savva and Dario Barresi
(Rocky Gravel), Rob Tuytel (Snow 02). See SOURCES.json for contribution roles.
Preview renders are downloaded from Poly Haven; they are not Demi captures.
