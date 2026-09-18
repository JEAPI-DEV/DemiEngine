# Prefabs And Scene Composition

Prefab source files end in `.prefab.json`, include `format_version`, and use a
stable `prefab://` ID. A reference such as `prefab://characters/player`
resolves to `prefabs/characters/player.prefab.json` beneath the owning project.

Prefabs contain `entities` and may contain nested `instances`. IDs in the
entity collection are local to the prefab. Expansion prefixes them with the
instance chain, so local entity `body` instantiated as `player` becomes
`player/body`. Transform parent references to another local entity are remapped
to the expanded stable ID.

Scene documents instantiate prefabs with:

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
- An override cannot replace the expanded entity ID.

Nested prefab cycles are errors and diagnostics include the active file chain.

Useful commands:

```text
demi prefab inspect prefabs/player.prefab.json
demi scene expand scenes/main.scene.json
demi scene diff scenes/old.scene.json scenes/new.scene.json
```

`scene diff` compares expanded documents, so prefab changes and instance
overrides appear as concrete JSON-pointer changes.

## Local entity hierarchy

Prefer `children` for entities that belong together in one scene or prefab:

```json
{
  "id": "wall",
  "components": { "Transform3D": {} },
  "children": [
    {
      "id": "concrete",
      "components": { "Transform3D": { "position": [0, 1.5, 0] } }
    },
    {
      "id": "reinforcement",
      "components": { "Transform3D": { "position": [0, 1.5, 0] } }
    }
  ]
}
```

Nesting supplies the child's transform parent. Child transforms are local to the
parent, and use the same domain (`Transform3D`, `Transform2D`, or `IsoTransform`).
A child without a transform receives an identity transform in that domain.
The owning entity must have a spatial transform. Explicit conflicting parents,
mixed transform domains and nesting deeper than 128 levels are errors.

IDs remain document-wide, not relative paths: a prefab instance `house` produces
`house/wall` and `house/concrete`, not `house/wall/concrete`. Duplicate child IDs
are still errors. CLI expansion and runtime loading flatten the hierarchy through
the same composition path; cooked source may retain nesting.

Use explicit transform `parent` references for cross-prefab/scene relationships
or other links that cannot be expressed inside the source tree. Existing flat
documents remain supported. Cross-scene references still require the parent to
be available under the existing loading/validation rules; nesting does not add
deferred cross-scene resolution.

Editor child creation and local reparenting write nested entities. Edits, subtree
duplication/deletion and Undo/Redo preserve authored nesting. Saving an unrelated
field does not automatically convert an existing flat document.

## Runtime Instantiation

Lua uses the same expansion and component-validation path as scene loading:

```lua
local Prefab = require("demi.prefab")

local instance = Prefab.instantiate("prefab://enemies/grunt", {
  id = "wave_4_grunt_12",
  position = { 4, 2, 0 },
  overrides = {
    body = {
      name = "Elite Grunt",
    },
  },
  pooled = true,
})
```

The returned instance ID remains stable across pooled reuse. Expanded entities
retain IDs such as `wave_4_grunt_12/body`. `Prefab.release` accepts either the
instance ID or any entity ID belonging to it. A pooled release disables the
instance; its next matching instantiate resets components from authored prefab
data before enabling it. Without `pooled = true`, release destroys the instance
through the world command buffer.

## Scene Flow And Lifetimes

`Scene.load` and `Scene.reload` perform deferred full transitions. For a loading
screen, call `Scene.prepare(scene_id, additive)`, poll `Scene.progress()`, then
call `Scene.activate()`. `Scene.cancel()` discards a prepared or in-flight
transition without changing the active world. `Scene.unload(scene_id)` removes
an additive scene.
Entity and UI IDs are global across simultaneously loaded scenes; conflicts
fail activation and are reported by `Scene.error()`.

`Scene.set_persistent(entity_id, true)` keeps an entity across full transitions.
The same behavior can be authored with `"persistent": true`. Scene-owned
entities, UI scripts, and resource-reference groups are released together.
Transitions emit `scene_loaded`, `scene_unloading`, `scene_unloaded`, and
`active_scene_changed` events.

Resource groups use shared ownership: unloading one scene does not release an
asset still referenced by another scene. Resource acquisition is transactional;
if any acquire fails, already-acquired resources from that attempt are rolled
back and the previous ownership groups remain intact.

Run the aggressive lifecycle suite normally or under sanitizers with:

```text
ctest --preset linux-debug -R demi-runtime-lifetime-failure-tests
cmake --preset linux-asan
cmake --build --preset linux-asan --target demi-runtime-lifetime-failure-tests
ctest --preset linux-asan -R demi-runtime-lifetime-failure-tests
```

The sanitizer preset raises the repeated load/unload loop to 200 iterations
and enables ASan/LSan leak detection.

Scaled gameplay time is available through `Time.delta_time`, `Time.time`,
`Time.fixed_time`, and `Time.frame_count`. Loading screens and application
services can use `Time.unscaled_delta_time`. `Time.set_paused` and
`Time.set_scale` control gameplay time; application focus and suspend changes
emit `application_focus`, `application_blur`, `application_suspend`, and
`application_resume`.
