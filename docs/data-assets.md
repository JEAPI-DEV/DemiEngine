# Data Assets

`DataAsset` stores game-specific JSON such as dialogue, items, quests, enemy
definitions, and balance values without adding engine components. The asset
pipeline validates and packages it; Lua receives detached read-only snapshots
of the canonical document.

## Authoring

The source document must be UTF-8 JSON with `format_version: 1`. Its manifest
declares a stable ID, content type, optional tags, and an optional `DataSchema`:

```json
{
  "format_version": 1,
  "id": "asset://items/iron_sword",
  "type": "DataAsset",
  "source": "iron_sword.json",
  "importer": "json_data",
  "importer_version": 1,
  "source_hash": "fnv1a64:...",
  "dependencies": ["asset://schemas/item"],
  "settings": {
    "schema": "asset://schemas/item",
    "content_type": "item",
    "tags": ["shop:forest"]
  }
}
```

Use `demi asset reimport path/to/iron_sword.asset.json` after editing its
source. `demi validate` checks versions, required properties, types, enums,
numeric ranges, schema-declared references, dependencies, and source hashes.
Cook and package commands carry the document, schema, and transitive asset
dependencies automatically.

The command below creates a `DataAsset` with the generic content type `data`:

```bash
demi asset import content.json --project demi.project.json --id asset://data/content
```

Give it a domain-specific `content_type`, tags, and schema as the project's
content model becomes clearer.

Schemas are JSON assets with type `DataSchema`. Supported constraints are
`type`, `required`, `properties`, `items`, `enum`, `minimum`, `maximum`, and
`reference` (`asset`, `prefab`, or `scene`). Validation paths include the exact
JSON pointer that failed.

## Lua

```lua
local item, error = Data.load("asset://items/iron_sword")
if item == nil then
  Debug.log(error.code .. ": " .. error.message)
end

local shop_items = Data.query({
  content_type = "item",
  tags = {"shop:forest"},
})
```

Queries are ordered by stable asset ID. Arrays remain one-based Lua arrays;
`Data.kind(value)` distinguishes empty arrays from empty objects. JSON null is
represented by `Data.null` and tested with `Data.is_null(value)`.

Each load returns a detached table. Mutating it never mutates engine content.
When a valid watched edit is accepted, `Data.revision(id)` increments and an
`Events` event named `data_asset_reloaded` supplies `id`, `old_revision`,
`new_revision`, and `affected_dependents`. Existing snapshots do not change.
An invalid reload keeps the last valid revision.

`Data.parse_yaml(text, source)` converts YAML mappings, sequences, and scalars
into the same detached Lua value contract. Parsing is generic engine
infrastructure backed by pinned `yaml-cpp`; locale selection and other domain
policy remain in optional packages. Invalid YAML returns a structured
`DataError` and observes the normal data-document size, depth, element, and
string limits.

Optional pure-Lua packages for flags, conditions, inventories, quests, and
dialogue live in `scripts/runtime/demi/data`. They accept explicit save state,
use stable IDs, and return structured gameplay events rather than controlling
a particular HUD.

See `examples/main_menu_animated` for a small schema-backed runtime probe.
