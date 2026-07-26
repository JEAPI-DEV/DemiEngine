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

## Runtime Instantiation

Lua uses the same expansion and component-validation path as scene loading:

```lua
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
