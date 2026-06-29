# AI-Friendly C++/Lua Game Engine Plan

## Goal
Create a Linux-focused game engine/framework for 2D and isometric 2.5D games that is practical for AI agents and humans to operate. The engine should provide a C++20 runtime, Lua gameplay scripting, a Unity-like editor workflow with Scene/Game views and Play/Pause/Stop controls, simple save/load support, image/video/cutscene playback, and optional multiplayer/networking primitives without building every subsystem from scratch.

## Confirmed Decisions
- Language split: C++20 for engine/runtime/editor, Lua 5.4 for game logic.
- Main platform: Linux only for now.
- Game focus: 2D and isometric/orthographic 2.5D, not true 3D physics.
- Editor workflow: single editor app with embedded runtime play mode, docked Scene view, docked Game view, inspector, hierarchy, asset browser, and Lua hot reload.
- Rendering: use existing rendering libraries rather than writing a renderer from scratch.
- Networking: optional ENet module with transport/replication primitives; no accounts, matchmaking, backend, or live-service platform in the first version.
- Saves: versioned JSON save files by default; optional SQLite module for complex games.
- Video/cutscenes: FFmpeg is required for first-class video playback.
- Build: CMake with hybrid dependency management; prefer system packages for heavy Linux dependencies and pinned vendored/FetchContent dependencies for small libraries.

## Recommended Dependency Stack
Use these as the default links in the initial README.

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
- RmlUi, optional runtime UI after MVP: https://github.com/mikke89/RmlUi

## Architecture

### Runtime Layers
1. `core`: logging, assertions, filesystem, time, UUIDs, handles, config, diagnostics.
2. `platform`: SDL3 windows, input, controller support, Linux platform services.
3. `renderer`: bgfx initialization, 2D batching, sprite atlases, tilemaps, cameras, render targets, debug drawing.
4. `scene`: entities, components, prefabs, scene loading/saving, deterministic serialization.
5. `simulation`: fixed timestep, update phases, events, timers, deterministic smoke-test hooks.
6. `physics2d`: Box2D world integration, colliders, rigid bodies, triggers, raycasts, debug draw.
7. `scripting`: Lua VM, sol2 bindings, script lifecycle, hot reload, Lua error reporting.
8. `assets`: asset registry, import metadata, dependency graph, texture/audio/video import, hot reload.
9. `media`: image display, audio playback, FFmpeg-backed video texture playback, cutscene control.
10. `save`: JSON save slots, versioning, migrations, optional SQLite backend.
11. `network`: optional ENet transport, simple client/server sessions, replicated properties/events.
12. `editor`: Dear ImGui shell, viewport panels, inspectors, hierarchy, asset browser, command history.
13. `cli`: machine-operable commands for validation, schema export, asset import, scene inspection, tests, and cooking.

### Isometric 2.5D Model
- Treat 2.5D as 2D gameplay with orthographic/isometric rendering.
- Use Box2D for gameplay collision on the logical `x/y` plane.
- Use draw depth, sprite layer, tile coordinate, and optional visual height fields for sorting and presentation.
- Provide built-in components for base-building games: `Grid`, `Tilemap`, `IsoTransform`, `Buildable`, `PlacementRules`, `Faction`, `Health`, and `Selectable`.
- Avoid adding true 3D physics in the first version. Keep Jolt Physics out of the initial plan unless true 3D becomes a requirement later.

## AI-Operability Requirements

### Source Data Formats
Use deterministic, human-readable files for all authoring data:
- `project.demiproject.toml`
- `assets/**/*.asset.yaml` or `.json`
- `scenes/**/*.scene.yaml` or `.json`
- `prefabs/**/*.prefab.yaml` or `.json`
- `scripts/**/*.lua`
- `saves/**/*.save.json`

Rules:
- Stable IDs for entities, assets, scenes, prefabs, and scripts.
- URI-style references such as `asset://textures/unit.png`, `scene://maps/village.scene`, `script://units/archer.lua`.
- Explicit `format_version` in every serialized file.
- Stable key ordering and formatting to make diffs AI-friendly.
- Generated/cooked files must be clearly separated from source authoring files.

### Reflection And Schemas
- Implement a C++ reflection metadata layer for serializable components and Lua-exposed APIs.
- Use reflection as the single source of truth for serialization, editor inspectors, validation, Lua bindings, documentation, and schema generation.
- Generate JSON Schema files for project files, scenes, prefabs, assets, and saves.
- Generate LuaLS/EmmyLua stubs for engine Lua APIs so AI agents and IDEs can understand available functions and types.

### CLI First
Create a `demi` CLI so agents can operate the engine without a GUI.

Required commands:
- `demi validate`
- `demi schema export`
- `demi lua-stubs generate`
- `demi asset import <asset>`
- `demi asset deps <asset>`
- `demi scene list`
- `demi scene inspect <scene>`
- `demi scene diff <old> <new>`
- `demi script check <script>`
- `demi test`
- `demi cook --target linux-debug`
- `demi run --project <project>`
- `demi editor --project <project>`

CLI output must support `--format text`, `--format json`, and later `--format sarif`.

### Diagnostics
All validation and runtime errors should include:
- severity
- stable error code
- file path
- line/column when available
- entity or asset ID when available
- clear message
- suggested fix when possible

## Editor Plan

### Required MVP Panels
- Scene viewport with camera controls, grid overlay, selection, transform gizmos, and physics/debug overlays.
- Game viewport rendered from the active game camera.
- Play/Pause/Stop toolbar using embedded runtime play mode.
- Scene hierarchy with stable entity IDs visible or copyable.
- Inspector generated from component reflection metadata.
- Asset browser backed by the asset registry.
- Console/diagnostics panel showing C++, Lua, asset, validation, and runtime errors.
- Lua hot reload controls and script error stack traces.

### Editor Principles
- No hidden editor-only scene state.
- Every editor action should map to an operation that can be logged, replayed, and eventually exposed through the CLI.
- The editor should call the same validation systems as `demi validate`.
- Play mode should run from a copy/snapshot of the editing scene so Stop can restore edit state.

## Lua Gameplay API

### Script Lifecycle
Support a consistent lifecycle:
```lua
function MyComponent:on_create() end
function MyComponent:on_start() end
function MyComponent:on_update(dt) end
function MyComponent:on_fixed_update(dt) end
function MyComponent:on_destroy() end
```

### Initial Lua Services
Expose small, explicit APIs:
- `Entity`
- `Scene`
- `Transform`
- `Input`
- `Physics2D`
- `Audio`
- `Video`
- `Assets`
- `Timer`
- `Events`
- `Save`
- `Debug`
- `UI` later, if RmlUi is added

### Lua Safety Rules
- Avoid exposing raw C++ internals.
- Use stable handles and explicit component names.
- Report Lua errors with script path, entity ID/name, function name, stack trace, and suggested fix where possible.
- Hot reload should preserve serialized script properties when possible and clearly report incompatible state changes.

## Save/Load Plan
- Default saves are JSON files with `format_version`, game ID, slot metadata, and serialized game state.
- Provide simple APIs: `Save.write(slot, table)`, `Save.read(slot)`, `Save.exists(slot)`, `Save.delete(slot)`.
- Add C++ support for schema validation and migrations.
- Keep scene files, editor project files, and runtime save files separate.
- Add optional SQLite module later for games with large indexed state, city/base data, or long-running simulations.

## Media Plan
- Load images/textures through the asset pipeline.
- Use miniaudio for audio playback and synchronization.
- Use FFmpeg for video decoding into bgfx textures.
- Provide Lua APIs for cutscenes: play, pause, skip, fade, subtitle hooks, completion events.
- Add a fallback image-sequence-plus-audio cutscene format for deterministic testing and simple projects.

## Networking Plan
- Add ENet as an optional module, not required for all games.
- Provide session primitives: host, connect, disconnect, send reliable/unreliable message, latency stats.
- Provide simple replication helpers for selected component fields.
- Keep authoritative gameplay examples simple and local-network friendly.
- Explicitly exclude accounts, matchmaking, NAT traversal, backend persistence, anti-cheat, and live operations from the first version.

## Repository Layout
```text
DemiEngine/
  CMakeLists.txt
  README.md
  AGENTS.md
  cmake/
  external/
  engine/
    core/
    platform/
    renderer/
    scene/
    simulation/
    physics2d/
    scripting/
    assets/
    media/
    save/
    network/
    editor/
    cli/
  schemas/
  docs/
  examples/
    minimal_2d/
    isometric_base_builder/
    fighting_game_2d/
  tests/
    cpp/
    lua/
    scenes/
    assets/
    smoke/
  tools/
  scripts/
```

## README Requirements
Create a root `README.md` during implementation with these sections:
- Project name and goal.
- What the engine is and is not.
- Linux-only first-platform statement.
- Supported game types: 2D and isometric 2.5D.
- AI-first design principles.
- Dependency list with GitHub links from this plan.
- High-level architecture diagram or bullet list.
- Build prerequisites and CMake build commands.
- How to launch the editor.
- How to run a sample game.
- How Lua scripting works.
- How scenes/assets/saves are stored.
- Validation and CLI commands.
- Roadmap and non-goals.

Also create `AGENTS.md` during implementation with:
- Build/test/validate commands.
- Source vs generated file policy.
- Scene/asset/schema editing rules.
- Lua API conventions.
- How to add a component.
- How to add an editor panel.
- How to run smoke tests.

## Implementation Phases

### Phase 1: Bootstrap And Documentation
- Create repository layout.
- Add root `README.md` and `AGENTS.md`.
- Add CMake project skeleton.
- Add dependency discovery/pinning strategy.
- Add minimal `demi` CLI executable.
- Add empty editor/runtime executable targets.
- Validate Linux build from a clean checkout.

### Phase 2: Core Runtime
- Implement logging, filesystem, UUIDs, diagnostics, config, and test harness.
- Add EnTT world wrapper and basic entity/component management.
- Add deterministic scene serialization format.
- Add JSON/TOML parsing and initial schemas.
- Add `demi validate` for project and scene files.

### Phase 3: Renderer And Windowing
- Integrate SDL3 and bgfx.
- Open a Linux window and render a clear color.
- Add sprite rendering, texture loading, cameras, render targets, and debug lines.
- Add isometric grid/tile rendering and depth sorting.
- Add screenshot/headless smoke-test hooks.

### Phase 4: Lua Scripting
- Embed Lua 5.4 and sol2.
- Bind entity, scene, transform, input, debug, timer, and events.
- Add script components and lifecycle hooks.
- Add Lua error reporting and hot reload.
- Generate LuaLS/EmmyLua stubs from bindings.
- Add `demi script check`.

### Phase 5: Editor MVP
- Integrate Dear ImGui docking.
- Add Scene view, Game view, hierarchy, inspector, asset browser, console, and toolbar.
- Implement embedded Play/Pause/Stop with edit-state restoration.
- Add ImGuizmo transform controls.
- Display validation errors inline.

### Phase 6: Physics, Audio, Media, Saves
- Integrate Box2D and physics components.
- Add miniaudio playback and audio components.
- Integrate FFmpeg video playback to bgfx textures.
- Add cutscene Lua API.
- Add JSON save/load APIs, save versioning, and migration hooks.

### Phase 7: Asset Pipeline
- Add asset metadata files and registry.
- Add import/reimport commands.
- Add texture/audio/video asset types.
- Add dependency graph queries.
- Add cooked asset output directory and generated-file policy.

### Phase 8: Optional Networking
- Add ENet module guarded by a CMake option.
- Add host/connect/disconnect and message APIs.
- Add basic replicated component examples.
- Add local smoke test for two clients or simulated peers.

### Phase 9: Example Games
- `minimal_2d`: movement, sprite, input, Lua script, save file.
- `isometric_base_builder`: isometric grid, placement rules, selectable buildings, JSON save/load.
- `fighting_game_2d`: 2D movement, hitboxes, animation states, audio, Lua gameplay scripts.
- Each example must run through editor play mode and CLI smoke tests.

## Validation Plan
- `cmake --build` succeeds on Linux.
- `demi validate` passes for all examples.
- `demi schema export` produces deterministic schemas.
- Lua stubs are generated and checked into the appropriate generated or source location per policy.
- Scene serialization round-trips without unstable diffs.
- Editor opens, displays Scene/Game views, enters Play mode, exits Play mode, and restores edit state.
- Physics smoke scene runs deterministically enough for regression checks.
- Video smoke test decodes a short FFmpeg-backed clip to a texture.
- Save/load smoke test writes, reads, migrates, and rejects invalid save data.
- Optional ENet smoke test runs only when networking is enabled.

## Risks And Mitigations
- bgfx shader/toolchain complexity: start with a tiny renderer sample and document shader build commands early.
- FFmpeg licensing/build complexity: document required Linux packages and build flags in README; keep media code isolated in `engine/media`.
- Reflection duplication risk: make reflection metadata the source for schemas, inspectors, docs, serialization, and Lua stubs.
- Editor/runtime divergence: editor must use the same scene/runtime systems as the game executable.
- AI-unfriendly hidden state: keep source files deterministic and validate all generated/cooked data boundaries.
- Scope creep toward Unity clone: keep first version focused on 2D/isometric games, Lua scripting, editor play mode, saves, media, and optional networking.

## Explicit Non-Goals For First Version
- Windows/macOS/mobile/console support.
- True 3D gameplay physics.
- Full material graph or 3D renderer.
- Visual scripting.
- Online accounts, matchmaking, backend hosting, or live-service operations.
- Marketplace/plugin ecosystem.
- Full Unity feature parity.

## Open Questions For Later
- Exact scene format extension: YAML is readable, JSON is simpler to validate; choose during implementation after selecting parser libraries.
- Whether RmlUi should be included in MVP or delayed until runtime UI needs exceed Dear ImGui/debug UI.
- Exact dependency pinning mechanism for bgfx and FFmpeg on the target Linux distribution.
- Whether cooked assets should be committed for examples or generated during build/test.
