# Asset iteration, streaming, and package content

Demi's Step 7 asset pipeline separates authored manifests, deterministic cook
policy, and runtime resource ownership. Importers never upload resources and
resource loaders never resolve package versions.

## Importers and deterministic cooking

`AssetImporterRegistry` accepts in-tree and package-provided importer
descriptors. A descriptor declares its identity and version, versioned settings
schema, supported source extensions and asset types, generated output types,
thread-safety, and target platforms. Selection fails when no importer matches
or when multiple importers match without an explicit choice. Use
`demi asset import ... --importer <id>` to disambiguate.

`AssetCookGraph` computes keys from the importer identity/version, normalized
settings, source hashes, platform/profile, package content provenance, and the
keys of every dependency. It does not inspect file timestamps. Its reverse
graph identifies exactly which dependents become stale, while `AssetCookCache`
checks both metadata keys and output hashes before reporting a cache hit.

Asset settings may include deterministic platform overrides while retaining a
single stable ID:

```json
{
  "settings": {
    "compression": "bc7",
    "quality": 90,
    "platform_overrides": {
      "android": {"compression": "astc_6x6", "quality": 75}
    }
  }
}
```

Texture and font atlases use ordinary `*.asset.json` manifests with the
registered `texture_atlas` and `font` importers. They are generated during a
normal `demi cook`; no example script or editor-only state is involved.

A texture-atlas asset points at a versioned JSON descriptor and declares every
source texture as a dependency:

```json
{
  "format_version": 1,
  "id": "asset://atlases/units",
  "type": "TextureAtlas2D",
  "source": "units.atlas.json",
  "importer": "texture_atlas",
  "importer_version": 2,
  "dependencies": ["asset://textures/archer"],
  "settings": {}
}
```

```json
{
  "format_version": 1,
  "page_width": 1024,
  "page_height": 1024,
  "padding": 2,
  "bleed": 1,
  "sprites": [
    {
      "id": "asset://sprites/archer/idle",
      "source": "asset://textures/archer",
      "pivot": [0.5, 1.0],
      "border": [0, 0, 0, 0],
      "animation_tag": "idle"
    }
  ]
}
```

Texture sources are currently PNG. Cooking emits deterministic atlas PNG pages
and `atlas.json` metadata. A `FontAtlas2D` manifest points directly at TTF/OTF
source and places `pixel_height`, `page_size`, `padding`, `glyph_ranges`, and
`fallbacks` in its settings; cooking emits font pages and `font-atlas.json`.

## Addressable groups

Scenes remain the source of truth for the assets they reference. Loading a
scene does not require a matching startup group, and scene-owned assets should
not be repeated in one. Addressable groups are optional lifetime boundaries for
content that must be loaded or unloaded together, such as the next
chapter, persistent UI/audio, or an optional voice pack.

Addressable groups use `*.asset-group.json`. This example defines an optional
voice pack that is not owned by the active scene:

```json
{
  "format_version": 1,
  "id": "asset-group://voice/de",
  "roots": ["asset://voice/de"],
  "budget": {
    "resident_mb": 256,
    "decoded_mb": 64,
    "upload_ms_per_frame": 3
  }
}
```

The initial scene automatically loads the assets referenced by its entities,
HUD, and expanded prefabs. Assets selected dynamically by Lua can be declared
after `scenes` in `demi.project.json` when they must be ready before gameplay:

```json
{
  "scenes": [
    {"id": "scene://main", "path": "scenes/main.scene.json"}
  ],
  "assets": [
    "asset://ui/portrait",
    "asset-group://audio/common"
  ]
}
```

Each entry may be one `asset://` resource or an `asset-group://` batch. These
entries load before `on_create` and `on_start`. Undeclared, non-scene assets
remain unloaded until `Assets.load`, so projects do not pay for every manifest
in their registry.

Lua uses one API for individual resources and batches:

```lua
local texture_request = Assets.load("asset://textures/portrait")
local chapter_request = Assets.load("asset-group://chapter_02")

if Assets.is_ready(chapter_request) then
  -- The group is already resident; no separate activation call is needed.
end

Assets.unload("asset://textures/portrait")
Assets.unload("asset-group://chapter_02")
```

The URI scheme is the type tag: `asset://` loads one resource and its
dependencies, while `asset-group://` loads every root in the group. Both remain
non-blocking and return a request for progress or cancellation.

Scene-rooted groups use `scene://chapter_02` as the root. The runtime loads the
scene through the normal scene loader, includes expanded prefab content, finds
the entity and UI asset references, and resolves their transitive asset
dependencies. CLI validation also verifies that the scene root is declared by
the project.

Scene transitions apply this rule automatically. Preparing a scene creates an
implicit group from its `scene://` root, waits for both the world and assets,
and activates them together at the frame boundary. Cancelling preparation
cancels both sides. A non-additive transition releases the outgoing scene's
implicit ownership after the incoming scene commits; additive scenes retain
their groups until unloaded.

The initial scene follows the same rule before Lua `on_start` and before the
first rendered frame. Asset-free scenes with no project `assets` continue
immediately.

Internally, `AssetGroupService` resolves transitive dependencies, performs read/decode work
off-thread through narrow `AssetResourceLoader` backends, and performs bounded
uploads from `update`. Preparation and activation remain separate engine
stages so scene transitions can commit atomically; the Lua facade combines
them into `load`. Shared resources, including resources requested
concurrently before the first upload completes, are decoded and uploaded once
and are unloaded only after their final active or preparing owner releases
them. Cancelling the request that started shared work hands it to another
waiting request. Progress is monotonic and reports `resolve`, `read`, `decode`,
`upload`, `ready`, `failed`, or `cancelled`. Memory reports identify resident
bytes, backend, and active owners per stable asset ID.

## Native ownership, reload, and recovery

`RegistryAssetResourceLoader` adapts the group lifetime contract to native
backends. The loader tracks stable IDs, while bgfx and miniaudio remain the
sole owners of GPU and audio objects. Source access and verification happen on
the worker stage; backend replacement happens during the bounded upload stage.
The resident-source loader remains only for headless runs and asset types that
have no native backend.

The 2D renderer, 3D renderer, and audio system each register one narrow loader
with `RuntimeAssetService`. Script modules do not need a second asset cache:
`LuaScriptHost` already owns their scene lifecycle and releases the active
scene's implicit group when a non-additive transition commits.

Watched registry changes call `reloadChangedResidentAssets`, so ordinary load,
reload, and unload share the same ownership path. A failed backend replacement
restores both the previous registry and the previous native resource snapshot.
Low-memory notifications cancel non-active preparation work without evicting
active groups. `restoreResources` reapplies each native backend's current
resident snapshot after a graphics device or surface recreation.

The runnable [`asset_streaming_showcase`](../examples/asset_streaming_showcase/README.md)
keeps its initial scene asset-free and drives an optional, transitively rooted
SVG group through load, progress, cancellation, explicit reload, unload, and
memory reporting.

## Locked package content

Packages can declare reusable content without changing Step 4 resolution:

```json
{
  "files": ["assets/theme.asset.json", "extensions/theme-importer.json"],
  "asset_manifests": ["assets/theme.asset.json"],
  "engine_extensions": ["extensions/theme-importer.json"]
}
```

Both content lists must be subsets of `files`. Cooking reads only
`demi.packages.lock.json` and `.demi/packages`; it never contacts a registry or
selects versions. It verifies installed manifests against the lock, diagnoses
stable-ID collisions, validates extension platform support, and records the
source package and installed content hash for every cooked asset. Package
removal is rejected while authored references or cooked manifests retain that
package's assets.

The integration test builds a complete installed-and-locked content fixture
without a registry, cooks it twice for Linux and once for Android, and compares
the Linux manifests for reproducibility while checking package provenance on
both platforms.
