# File Formats

The scaffold currently uses JSON because it is easy to validate, parse, diff, and generate. YAML or TOML can be added later where they provide a clear benefit.

## Project Files

Project files use `*.project.json` and declare the project name, scenes, and metadata.

## Scene Files

Scene files use `*.scene.json` and declare stable scene IDs and entity lists.

Scenes can declare generic components such as `Transform2D`, `Camera2D`, `Sprite`, `LuaScript`, `Rigidbody2D`, and `BoxCollider2D`. Gameplay-specific behavior should live in Lua scripts rather than engine code.

## Save Files

Save files use `*.save.json` and declare save slot metadata plus game-specific state.

## Asset Manifests

Asset manifests use `*.asset.json` and map stable `asset://` IDs to source
files. New imports record all pipeline metadata:

```json
{
  "format_version": 1,
  "id": "asset://characters/hero",
  "type": "Texture2D",
  "source": "hero.png",
  "importer": "image",
  "importer_version": 1,
  "source_hash": "fnv1a64:...",
  "dependencies": ["asset://materials/hero"],
  "generated_output": "../../../generated/assets/characters/hero/hero.png",
  "settings": {
    "filter": "nearest",
    "wrap": "clamp",
    "mipmaps": false
  },
  "license": "License.txt",
  "attribution": "Artist name"
}
```

Use `demi asset reimport` to upgrade an older manifest before loading it.
Tools that produce sources under a project's `assets/` directory should use
`demi asset register-generated` instead of calculating `source_hash` or writing
manifest metadata themselves.
Current pass-through
importers support PNG, JPEG, BMP, TGA, QOI, PPM, SVG, GIF, WAV, OGG, MP3,
FLAC, glTF, GLB, OBJ, IQM, M3D, MP4, WebM, MOV, and JSON data. glTF URI sidecars are
discovered and carried through import, export, and cooking.

Texture-bearing assets accept `nearest`, `bilinear`, or `trilinear` filtering;
`repeat`, `clamp`, or `mirror` wrapping; and optional mipmap generation. The
same settings apply to standalone textures, explicit model textures, and
textures embedded in glTF materials.

### Render assets

`Material` manifests point at versioned `*.material.json` sources. The source
owns its shader ID, platform-independent fallback, named `asset://` texture
slots, number/color parameters, and blend/cull/depth state. `Shader` manifests
point at versioned `*.shader.json` sources containing vertex and fragment
paths plus validated Linux/Android fallback IDs. Shader stage files are
discovered as asset sidecars and therefore travel through cooking and package
export. See [Rendering And Effects](rendering-and-effects.md).

## Performance Budgets

Projects may document reference limits that profiling and CI can evaluate:

```json
"performance_budgets": {
  "maximum_frame_ms": 16.67,
  "maximum_draw_calls": 128,
  "maximum_resident_assets": 64
}
```

Budgets are authored project data. They do not silently alter rendering or
simulation quality.

Portable `*.demipack` files are deterministic binary containers with a
versioned JSON index followed by sorted file data. The index records stable
asset IDs, manifest paths, file sizes, and checksums. Packages contain selected
assets, transitive asset dependencies, raw sources, generated outputs, and
declared license/attribution files. Import rejects absolute paths, `..` path
traversal, checksum failures, and implicit replacement of existing IDs/files.

Cooking writes runtime-ready project data under `generated/cooked/<platform>`
by default, with `cook.manifest.json` recording every included path and hash.
Cooked data and application bundles are generated output and must not be edited
by hand.

## Versioning

Every format includes `format_version`. Migrations should be explicit and test-covered.
