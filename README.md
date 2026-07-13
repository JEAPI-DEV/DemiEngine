# DemiEngine

DemiEngine is a Linux-first C++20 game engine for AI-assisted development, with
minimal experimental Android support. It focuses on deterministic project data,
Lua gameplay scripting, small reference games, and a command-line workflow that
agents can validate without hidden editor state.

The runtime is no longer just a placeholder. It can load JSON projects and scenes, run Lua 5.4 scripts, render 2D and simple 3D scenes through raylib, step 2D/3D physics helpers, play audio/media services where dependencies are enabled, save JSON-backed state, and drive HUD menus from data plus Lua actions.

It is still early. The editor executable exists as a boundary, not a finished tool.

## Current Output

These screenshots are from the checked-in examples. They are small probes, but they show the runtime pieces working together: JSON scenes, HUD data, Lua actions, rendering, and camera/player logic.

![Minimal 2D networking menu](images/minimal_2d_networking.png)

`examples/minimal_2d_networking` uses a data-driven HUD menu, Lua action handlers, save-backed settings, scene switching, and optional networking paths.

![Minimal 3D runtime scene](images/minimal_3d.png)

`examples/minimal_3d` exercises the 3D renderer, scene loader, and Lua-controlled movement.

![Isometric base builder](images/isometric_base_builder.png)

`examples/isometric_base_builder` is a simpel demonstration of a game made with isometric graphics.
It features a grid-based building system with pathfinding and resource management, it is a playable (minimal) tower defense demo.

![Main menu animated](images/main_menu_animated.png)

`examples/main_menu_animated` is a polished block-adventure menu probe that loops a walking sprite behind the menu with `GuiAnimation`.

![Minimal voxel](images/minimal_voxel.png)

`examples/minimal_voxel` is a minimal voxel-based scene probe demonstrating basic 3D rendering and physics integration.

![Ui showcase](images/ui_showcase_1.png)

`examples/main_menu_animated` is a polished block-adventure menu probe that loops a walking sprite behind the menu with `GuiAnimation`.

## Docs

- [Generated docs PDF](https://github.com/JEAPI-DEV/DemiEngine/blob/main/docs/latex/main.pdf)
- [Architecture notes](docs/architecture.md)
- [CLI notes](docs/cli.md)
- [File formats](docs/file-formats.md)
- [Capability matrix](docs/capabilities.md)
- [Compatibility policy](docs/compatibility.md)

## What This Engine Is

- A Linux-focused C++20 runtime.
- A Lua-driven gameplay layer with high-level engine services.
- A schema-first project format built from deterministic JSON files.
- A production-focused engine for 2D, isometric/2.5D, and data-heavy/UI-heavy games.
- An experimental lightweight 3D runtime for small exploration, action, and puzzle games.
- A repo designed to be changed by humans and coding agents without editor-only state.

## What This Engine Is Not

- Not a Unity clone.
- Not a full editor yet.
- Not a live-service backend, account system, or matchmaking platform.
- Not a high-end Unity-style 3D renderer or editor.
- Not broadly cross-platform; Linux is the supported desktop target.

## Current Runtime Features

- Project, scene, HUD, asset, and save validation through the `demi` CLI.
- Scene loading from `*.scene.json`, including nested `components` data.
- HUD loading from `*.hud.json`, with buttons, text, rectangles, images, groups, visibility, hover state, click actions, and Lua-controlled position/size/opacity.
- `require("demi.gui_animation")`: a small Lua callback scheduler for HUD animation.
- `GifAnimation2D` assets loop using the GIF's native frame delays; `Icon2D` assets render SVG icons for HUD use.
- Lua 5.4 scripting through sol2.
- Lua lifecycle functions: `on_create`, `on_start`, `on_update`, `on_fixed_update`, and `on_destroy`.
- Lua action/event annotations: `@HandleAction("...")` and `@OnEvent("...")`.
- Lua services for `Debug`, `Input`, `Timer`, `Events`, `Scene`, `Runtime`, `Entity`, `Transform2D`, `Transform3D`, `Physics2D`, `Rigidbody2D`, `HUD`, `Save`, `Audio`, `Video`, `Cutscene`, `Network`, and `NetworkSession`.
- 2D rendering, HUD rendering, debug lines, and pixel-style text rendering.
- Basic 3D rendering and 3D collision helpers.
- Box2D-backed 2D physics.
- JSON save slots with versioned migration hooks.
- Optional ENet networking when `DEMI_ENABLE_NETWORK=ON`.
- FFmpeg-backed media support when `DEMI_ENABLE_MEDIA=ON`, which is the default.

## Runtime Layout

The runtime code is split by responsibility:

- `src/demi/runtime/app`: runtime loop, input polling, window setup, and subsystem orchestration.
- `src/demi/runtime/audio`: audio playback through miniaudio.
- `src/demi/runtime/media`: video/cutscene media plumbing.
- `src/demi/runtime/network`: optional network transport and session helpers.
- `src/demi/runtime/physics`: 2D and 3D movement/collision helpers.
- `src/demi/runtime/render`: 2D/3D renderers and font support.
- `src/demi/runtime/scene`: project loading, scene parsing, HUD parsing, JSON helpers, and runtime scene data.
- `src/demi/runtime/scripting`: Lua host lifecycle, services, diagnostics, loading, persistence, and bindings.
- `src/demi/runtime/scripting/bindings`: installable Lua binding modules.
- `src/demi/runtime/scripting/persistence`: save-slot parsing and serialization.

The scene loader is intentionally small now. `SceneLoader.cpp` acts as the facade, while `ProjectParser`, `SceneEntityParser`, `HudParser`, and `SceneJson` own the parsing details. Component parsing uses a small strategy table keyed by component name, so adding a scene component no longer means extending one long loader function.

Lua bindings follow the same idea. `LuaScriptHostBindings.cpp` installs binding modules; the module files own their own API surface. Save persistence is split behind `LuaSaveCodec`, with separate parsers for current JSON saves and legacy save data.

## Dependencies

Required for the default Linux debug build:

- CMake 3.22+
- Ninja
- GCC 12+ or Clang 15+
- PkgConfig
- Lua 5.4 development files
- FFmpeg development packages when `DEMI_ENABLE_MEDIA=ON` (the desktop default)

Fetched or linked by CMake:

- raylib 5.5: rendering, windowing, input, models, and platform layer
- Lua 5.4.7 and sol2: gameplay VM and C++/Lua binding
- Box2D 2.4.1: 2D physics
- miniaudio 0.11.22: audio playback
- nlohmann/json 3.11.3: project, scene, HUD, asset, and save parsing
- mbedTLS 3.6.2: TLS and DTLS security
- ENet 1.3.18: optional networking when `DEMI_ENABLE_NETWORK=ON`
- FFmpeg: system media libraries when `DEMI_ENABLE_MEDIA=ON`
- librsvg: optional system SVG rasterization support when available

## Build And Test

Use the presets:

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
```

The repo also has a release preset:

```bash
cmake --preset linux-release
cmake --build --preset linux-release
```

Headless runtime smoke:

```bash
DEMI_HEADLESS=1 ./build/linux-debug/demi run --project examples/minimal_2d_networking/demi.project.json --frames 1
DEMI_HEADLESS=1 ./build/linux-debug/demi run --project examples/minimal_3d/demi.project.json --frames 1
```

## CLI

After building:

```bash
./build/linux-debug/demi --help
./build/linux-debug/demi version
./build/linux-debug/demi validate examples/minimal_2d_networking/demi.project.json
./build/linux-debug/demi validate examples/minimal_3d/demi.project.json
./build/linux-debug/demi schema export
./build/linux-debug/demi scene list examples/minimal_2d_networking/demi.project.json
./build/linux-debug/demi scene inspect examples/minimal_2d_networking/scenes/menu.scene.json
./build/linux-debug/demi save inspect examples/minimal_2d_networking/saves/settings.save.json
./build/linux-debug/demi script check examples/minimal_2d_networking/scripts/main_menu.lua
./build/linux-debug/demi lua-stubs generate build/linux-debug/generated/demi.lua
./build/linux-debug/demi run --project examples/minimal_2d_networking/demi.project.json
```

`demi editor --project <project>` exists as a command boundary. Treat the CLI as the automation contract for now.

## Examples

- `examples/minimal_2d_networking`: the main runtime probe. It has a data-driven HUD menu, Lua action handlers, scene switching, save-backed settings, platformer/slingshot levels, and optional networking code paths.
- `examples/minimal_3d`: a small 3D runtime probe with a Lua-controlled player script.
- `examples/main_menu_animated`: a polished block-adventure menu probe that loops a walking sprite behind the menu with `GuiAnimation`.
- `examples/main_menu_gif`: a portrait mobile-hub probe using the supplied animated GIF wallpaper and SVG icon assets.
- `examples/fighting_game_2d`: early fighting-game data and Lua scripts.
- `examples/isometric_base_builder`: early isometric/base-builder scene data.

Examples are not throwaway demos. They are probes for engine features. If an example needs a behavior that belongs in the engine, add the reusable runtime feature instead of patching the script around it.

## File Formats

Authoring data stays deterministic and text-based:

- Projects: `demi.project.json`
- Scenes: `*.scene.json`
- HUD files: `*.hud.json`
- Saves: `*.save.json`
- Assets: `*.asset.json`
- Lua scripts: `*.lua`

Every project, scene, save, and future asset manifest should include `format_version`. Generated files belong in `build/`, `generated/`, or `examples/**/generated/`.

Scene components live under an entity's `components` object:

```json
{
  "id": "ent_menu_controller",
  "name": "Menu Controller",
  "components": {
    "LuaScript": {
      "module": "script://scripts/menu_scene.lua"
    }
  }
}
```

HUD button actions are plain data:

```json
{
  "type": "button",
  "id": "menu_button_network",
  "label": "NETWORK PLAY",
  "action": "menu_button_network"
}
```

Lua can bind that action with an annotation:

```lua
-- @HandleAction("menu_button_network")
function Actions.show_network()
  -- ...
end
```

## Lua API

Public Lua API stubs live in `scripts/stubs/demi.lua`. Regenerate a copy for tools with:

```bash
./build/linux-debug/demi lua-stubs generate build/linux-debug/generated/demi.lua
```

Lua scripts should use explicit lifecycle functions:

```lua
local Player = {}

function Player:on_start()
  Debug.log("ready")
end

function Player:on_update(dt)
  local x, y = Input.vector("a", "d", "s", "w")
  Transform2D.add_position(self.entity_id, x * dt * 6.0, y * dt * 6.0)
end

return Player
```

Keep gameplay scripts high-level. Use `Entity`, `Transform2D`, `Transform3D`, `Network`, `NetworkSession`, `HUD`, and save/runtime services instead of raw C++ internals.

## Tests

The test suite covers validation, Lua scripting, Lua stubs, scene loading, physics, networking paths, runtime smoke, and example script checks.

```bash
ctest --preset linux-debug --output-on-failure
```

Useful focused checks:

```bash
./build/linux-debug/demi-scene-loader-tests .
./build/linux-debug/demi-lua-scripting-tests
./build/linux-debug/demi-physics2d-tests
./build/linux-debug/demi-physics3d-tests
```

`demi-scene-loader-tests` specifically guards the nested scene component shape and HUD button action loading used by the menu example.

## Development Notes

- Keep public APIs small and explicit.
- Prefer stable IDs and URI-style references: `scene://main`, `asset://textures/unit.png`, `script://scripts/player.lua`.
- Update components as a full slice: C++ data, scene parsing, validation/schema, Lua bindings if public, stubs, and tests.
- Keep runtime data serializable. If state matters outside one frame, it probably belongs in scene/project/save data, not a hidden script global.
- Run validation after editing example data.
