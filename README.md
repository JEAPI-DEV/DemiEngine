# DemiEngine

DemiEngine is a Linux-first game engine scaffold for AI-assisted development of 2D and isometric 2.5D games. The goal is a C++20 engine/runtime with Lua gameplay scripting, deterministic text-based project data, schema validation, and a Unity-like editor workflow with Scene and Game views.

This repository currently implements the first buildable scaffold plus an SDL3-backed runtime preview: documentation, CMake targets, a minimal `demi` CLI, validation contracts, schemas, example project files, typed scene loading, player movement, Lua lifecycle calls, and a visible debug scene view. Full bgfx rendering, editor UI, physics, media, saves, and networking are still represented as documented subsystem boundaries before their external dependencies are wired in.

## What This Engine Is

- A Linux-focused C++20 engine framework.
- A Lua-driven game development environment.
- A planned editor with embedded Play/Pause/Stop mode.
- A schema-first project format designed for humans and AI agents.
- A focused engine for 2D and isometric 2.5D games such as base builders and 2D fighting/action games.

## What This Engine Is Not

- Not a Unity clone.
- Not a general 3D engine in the first version.
- Not a live-service backend, account system, or matchmaking platform.
- Not cross-platform yet.

## Planned Dependency Stack

- SDL3, platform/input/windowing: https://github.com/libsdl-org/SDL
- bgfx, renderer abstraction: https://github.com/bkaradzic/bgfx
- Dear ImGui, editor UI/docking: https://github.com/ocornut/imgui
- ImGuizmo, scene transform gizmos: https://github.com/CedricGuillemet/ImGuizmo
- Lua, gameplay VM: https://github.com/lua/lua
- sol2, C++/Lua binding: https://github.com/ThePhD/sol2
- EnTT, ECS/runtime registry: https://github.com/skypjack/entt
- Box2D, 2D physics: https://github.com/erincatto/box2d
- miniaudio, audio playback/mixing: https://github.com/mackron/miniaudio
- FFmpeg, video/cutscene decoding: https://github.com/FFmpeg/FFmpeg
- ENet, optional reliable UDP networking: https://github.com/lsalzman/enet
- nlohmann/json, JSON data/save support: https://github.com/nlohmann/json
- toml++, project/config files: https://github.com/marzer/tomlplusplus
- SQLite, optional advanced save/editor DB: https://github.com/sqlite/sqlite
- Tracy, profiling: https://github.com/wolfpld/tracy
- Native File Dialog Extended, native file dialogs: https://github.com/btzy/nativefiledialog-extended
- RmlUi, optional runtime UI: https://github.com/mikke89/RmlUi

## Current Architecture

- `src/demi/core`: version and shared core definitions.
- `src/demi/diagnostics`: structured diagnostics and CLI rendering.
- `src/demi/filesystem`: project path discovery helpers.
- `src/demi/schema`: lightweight scaffold validation for project, scene, and save files.
- `src/demi/runtime`: project/scene loading and SDL3 debug rendering.
- `src/demi/assets`: project-local asset manifest loading and `asset://` resolution.
- `scripts/stubs`: initial LuaLS annotations for the exposed runtime Lua API.
- `src/cli`: `demi` command-line interface.
- `src/runtime`: placeholder runtime executable.
- `src/editor`: placeholder editor executable.
- `schemas`: source schemas for project, scene, and save files.
- `examples`: AI-editable example projects and scenes.
- `tests`: smoke tests for the scaffold.

## Build

Prerequisites:

- Linux
- CMake 3.25 or newer
- Ninja
- A C++20 compiler such as GCC 12+ or Clang 15+

Build with presets:

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
```

Fallback without presets:

```bash
cmake -S . -B build/linux-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/linux-debug
ctest --test-dir build/linux-debug --output-on-failure
```

## CLI

After building, run:

```bash
./build/linux-debug/demi --help
./build/linux-debug/demi validate examples/minimal_2d/demi.project.json
./build/linux-debug/demi schema export
./build/linux-debug/demi asset inspect examples/minimal_2d/assets/sprites/player.asset.json
./build/linux-debug/demi scene list examples/minimal_2d/demi.project.json
./build/linux-debug/demi save inspect examples/minimal_2d/saves/smoke.save.json
./build/linux-debug/demi run --project examples/minimal_2d/demi.project.json
./build/linux-debug/demi editor --project examples/minimal_2d/demi.project.json
```

The CLI is the automation contract for AI agents. Editor actions should eventually map to CLI-compatible operations or replayable editor commands.

## Runtime Preview

Launch the visible game/view window with:

```bash
./build/linux-debug/demi run --project examples/minimal_2d/demi.project.json
```

The current runtime preview uses SDL3 and renders debug geometry from the main scene:

- `Camera2D.clear_color` sets the background.
- `Transform2D.position` controls placeholder entity placement.
- `Sprite.texture` resolves through project-local `*.asset.json` manifests. The minimal example loads a real `Texture2D` source from `examples/minimal_2d/assets/sprites/player.ppm`.
- `HitboxController` entities draw as red outlined rectangles.
- `IsoGrid` entities draw a simple isometric grid preview only when the scene declares an `IsoGrid` entity.
- The runtime exposes key state to Lua; the minimal example's `player.lua` chooses to move the player with WASD and arrow keys.
- Lua scripts attached with `LuaScript` are loaded from `script://` paths relative to the project directory and receive lifecycle callbacks.

For automated smoke tests, limit the window loop to a fixed number of frames:

```bash
SDL_VIDEODRIVER=dummy ./build/linux-debug/demi run --project examples/minimal_2d/demi.project.json --frames 1
```

## File Formats

Authoring data should remain deterministic and text-based:

- Projects: `*.project.json`
- Scenes: `*.scene.json`
- Saves: `*.save.json`
- Lua scripts: `*.lua`
- Future asset manifests: `*.asset.json`

All source formats must include `format_version`. Generated or cooked data belongs in `generated/` or `build/` and should not be hand-edited.

## Lua Direction

Gameplay code will be written in Lua 5.4. The current runtime exposes `Debug.log` and calls `on_create`, `on_start`, `on_update(dt)`, `on_fixed_update(dt)`, and `on_destroy` when those functions exist on the returned script table. The initial LuaLS stub is in `scripts/stubs/demi.lua`; future phases should generate it from C++ bindings.

## Roadmap

1. Core diagnostics, project formats, CLI validation, and examples.
2. SDL3 plus bgfx window and sprite rendering.
3. EnTT scene/entity/component runtime.
4. Lua 5.4 plus sol2 scripting lifecycle and hot reload.
5. Dear ImGui editor with Scene/Game views and embedded play mode.
6. Box2D physics, miniaudio audio, FFmpeg video, JSON save/load.
7. Asset pipeline and dependency graph.
8. Optional ENet networking module.
