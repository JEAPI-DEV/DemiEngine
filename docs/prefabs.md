# Prefabs And Scene Composition

Prefab source files end in `.prefab.json`, include `format_version`, and use a
stable `prefab://` ID. A reference such as `prefab://characters/player`
resolves to `prefabs/characters/player.prefab.json` beneath the owning project.

Prefabs may contain `entities`, HUD `elements`, and nested `instances`. IDs in
those collections are local to the prefab. Expansion prefixes them with the
instance chain, so local entity `body` instantiated as `player` becomes
`player/body`. Transform parent references to another local entity are remapped
to the expanded stable ID.

Scene and HUD documents instantiate prefabs with:

```json
{
  "instances": [{
    "id": "player",
    "prefab": "prefab://characters/player",
    "overrides": {
      "body": {
        "components": {
          "Transform2D": { "position": [4, 8] }
        }
      }
    }
  }]
}
```

Override semantics are deterministic:

- An absent field inherits the prefab value.
- Objects merge recursively.
- Arrays replace the inherited array completely.
- `null` removes the inherited field. Setting a component to `null` therefore
  removes that component; setting an entity override to `null` removes the
  entity from that instance.
- An override cannot replace the expanded entity or element ID.

Nested prefab cycles are errors and diagnostics include the active file chain.

Useful commands:

```text
demi prefab inspect prefabs/player.prefab.json
demi scene expand scenes/main.scene.json
demi scene diff scenes/old.scene.json scenes/new.scene.json
```

`scene diff` compares expanded documents, so prefab changes and instance
overrides appear as concrete JSON-pointer changes.
