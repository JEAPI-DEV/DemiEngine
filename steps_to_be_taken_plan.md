# DemiEngine: Steps To Be Taken

## Purpose

Build DemiEngine into a deterministic, JSON-authored, Lua-first game engine for
2D, isometric/2.5D, UI-heavy, and lightweight 3D games. The goal is broad game
capability, not Unity feature parity or Unity's workflow.

The product principles are:

- JSON is the durable, reviewable source of projects, scenes, HUD/UI, prefabs,
  assets, and saves.
- Lua owns game rules and high-level behaviour; C++ owns stable engine systems.
- The CLI, validator, runtime, and eventual editor use the same data and
  diagnostics paths.
- Each example is an engine probe. A missing capability is fixed in reusable
  engine code, never by a one-off script workaround.
- Every feature has a small deterministic example and automated validation.

## Current Baseline

Already working: JSON project/scene/HUD loading and validation, Lua lifecycle
and services, 2D and lightweight 3D rendering, Box2D, basic 3D collision,
audio, video, JSON saves, optional networking, CLI tooling, and headless smoke
tests.

The present limiting factor is composition. The runtime world stores a fixed
set of optional C++ component fields, while examples can declare unknown
components. New game genres would therefore grow the engine through special
cases. The next work must make component authoring and reuse scalable.

## Milestone 0 — Establish The Contract

**Status: complete.** The architecture, product tiers, compatibility policy,
dependency list, and capability matrix now describe the implemented engine.

1. Update `docs/architecture.md`, `README.md`, and `plan.md` to match the
   actual implementation: raylib, sol2, current 3D support, and the current
   dependency set. Remove obsolete SDL3/bgfx/EnTT future-tense statements.
2. Document supported tiers:
   - **Production focus:** 2D, isometric/2.5D, and data-heavy/UI-heavy games.
   - **Supported experimental tier:** lightweight 3D.
   - **Explicitly deferred:** high-end Unity-style 3D rendering/tooling.
3. Create a compatibility policy: scene/prefab/HUD/save formats change only
   through versioned migrations and a documented deprecation window.
4. Add a capability matrix to the documentation showing which features are
   stable, experimental, or planned.

**Done when:** a contributor can read the docs and accurately predict what the
engine builds with and which game types it targets.

## Milestone 1 — Component Metadata And Runtime World

**Status: complete.** Components own their field/editor/Lua metadata, the
generated registry drives parsing, validation, and canonical schema export,
and entities use type-keyed component storage. Unknown components are rejected;
declared arbitrary gameplay state uses `GameplayData.values`.

1. Define a `ComponentDescriptor` registry in C++ for every engine component.
   It must contain the component name, JSON parser/serializer, validation
   rules, defaults, Lua exposure policy, and editor metadata placeholder.
2. Make the registry the single path used by scene loading and validation.
   Do not maintain separate hand-written lists in parser, schema, stubs, and
   future inspector code.
3. Replace the expanding fixed optional-component layout in `SceneData.h` with
   component storage keyed by entity ID and component type. Keep simple typed
   C++ accessors so the renderer and physics code remain clear.
4. Define component ownership rules:
   - Engine components are parsed and validated by C++.
   - Gameplay data belongs in `LuaScript.properties`, save data, or a declared
     generic serializable gameplay component; it must not silently disappear.
   - Unknown components are validation errors unless a registered extension
     explicitly owns them.
5. Generate/maintain the JSON Schema component definitions from this metadata,
   or make the schema import a generated canonical component schema.
6. Add tests for component parsing, defaults, invalid fields, serialization
   round-trip stability, and Lua visibility.

**Done when:** adding one engine component touches one component module plus
tests, rather than SceneData, parser, schema, bindings, stubs, and renderer
lists independently.

## Milestone 2 — Prefabs And Scene Composition

1. Add versioned `*.prefab.json` files with stable entity/component IDs.
2. Add scene instances that reference `prefab://` IDs and provide deterministic
   component-field overrides.
3. Define override semantics before implementation: absent means inherited,
   explicit `null`/remove has one documented meaning, and arrays have a clear
   replacement/merge policy.
4. Support nested prefabs and detect cycles with useful diagnostics.
5. Add CLI commands:
   - `demi prefab inspect <prefab>`
   - `demi scene expand <scene>`
   - `demi scene diff <old> <new>` (replace the current placeholder)
6. Convert one player, one prop, and one UI element in existing examples to
   prefabs. Do not bulk-convert everything until the semantics are proven.

**Done when:** a developer can create a reusable player/enemy/building prefab,
override properties per instance, and get deterministic expanded scene output.

## Milestone 3 — Authorable Runtime UI

1. Preserve `*.hud.json`, but evolve it from a flat list of positioned drawing
   primitives into a tree of elements with parent/child relationships.
2. Add layout primitives: anchors, margins/padding, min/max size, alignment,
   row/column containers, grid containers, and resolution scaling.
3. Add practical controls: label, image, button, toggle, slider, text input,
   scroll container, list, progress bar, and modal/focus layer.
4. Add controller/keyboard focus navigation, pointer capture, disabled state,
   accessibility labels, and action maps for UI navigation.
5. Add reusable JSON styles/themes and localization keys. Keep content strings
   outside layout files where possible.
6. Add Lua APIs for setting data/state and handling actions/events; layout must
   remain data driven rather than Lua moving widgets every frame.
7. Produce a menu/settings/inventory example that works at 16:9, 4:3, and a
   portrait mobile resolution in headless layout tests.

**Done when:** a complex menu can be authored as JSON without hard-coded pixel
coordinates or custom Lua layout code.

## Milestone 4 — Complete The 2D Gameplay Foundation

1. Add input actions and bindings in project JSON. Gameplay scripts refer to
   actions such as `move_left` and `jump`, not raw keyboard names.
2. Add sprite sheets, clips, animation playback, events, flip, pivot, sorting
   layers, and camera follow/bounds.
3. Add tilemap assets, tile layers, tile collision generation, parallax, and
   an efficient rendering path.
4. Complete 2D physics ergonomics: circle/capsule/polygon colliders as needed,
   collision filtering matrix, trigger callbacks, queries, raycasts, overlaps,
   and joints.
5. Provide a reusable character-controller package built on the above APIs;
   keep genre behaviour in Lua rather than baking it into the runtime.
6. Convert the platformer example into a complete reference vertical slice.

**Done when:** a new 2D game can use tilemaps, animated sprites, action input,
physics queries, and reusable player logic with no engine changes.

## Milestone 5 — Saves, Simulation, And Debuggability

1. Define explicit serializable game state: game-level state, selected entity
state, prefab instance state, and Lua tables deliberately nominated for saving.
2. Add save schemas, migration registration, autosave metadata, and clear
diagnostics for incompatible saves.
3. Add fixed-timestep configuration and deterministic random-number services
for games that need reproducible simulation.
4. Expand the profiler into a developer-facing HUD/CLI report covering frame,
Lua, render, physics, asset loading, and networking time.
5. Add debug overlays controlled by project/runtime flags: colliders, contacts,
navigation/grid, entity IDs, draw order, and UI bounds.
6. Add replayable input fixtures for headless regression tests.

**Done when:** an example can save/load meaningful game state, replay a short
test input sequence, and produce actionable performance/debug information.

## Milestone 6 — Isometric, Strategy, And Builder Systems

1. Turn `IsoGrid` and `IsoTransform` into real runtime/rendering systems with
grid-to-world conversion, depth sorting, selection, and debug visualization.
2. Add grid occupancy, placement previews, placement rules, build costs, and
deterministic failure diagnostics.
3. Add pathfinding as a reusable grid/navigation service, with incremental or
asynchronous calculation only after a correct synchronous baseline exists.
4. Add selection, command queues, factions/teams, health/resource components,
and data-driven interaction hooks where they are generally useful.
5. Complete `examples/isometric_base_builder` as a playable build/place/save
vertical slice.

**Done when:** the base-builder is no longer declarative-only scene data but a
small playable game using reusable grid and placement systems.

## Milestone 7 — Animation And Combat Primitives

1. Create a common animation state-machine interface for 2D sprites and 3D
models: named states, transitions, parameters, events, and Lua control.
2. Implement hitbox/hurtbox windows as data/assets timed by animation events;
do not keep combat collision as an example-only `HitboxController` convention.
3. Add input buffering and command recognition as optional Lua-level helpers.
4. Define a frame-stepped simulation mode and snapshot boundary suitable for
future rollback experiments, without committing to full rollback netcode yet.
5. Complete `examples/fighting_game_2d` as a local two-player combat slice.

**Done when:** combat games can author animation-timed attacks and test them
without bespoke C++ components per fighter.

## Milestone 8 — Asset Pipeline And Shipping Workflow

1. Expand asset manifests with importer settings, source hash, dependencies,
generated output location, and importer version.
2. Implement deterministic `demi asset import`, reimport, dependency, and
cooking commands. Never hand-edit cooked data.
3. Support production image/audio/model formats through importers while keeping
source manifest references stable as `asset://` IDs.
4. Add asset validation for missing files, invalid references, cyclic
dependencies, unsupported platform formats, and stale generated data.
5. Add `demi cook` and packaging for Linux first, then improve the existing
Android path only after its runtime capability is covered by tests.
6. Add a clean-checkout build/cook/run CI job.

**Done when:** a project can be validated, cooked, and run from a clean
checkout without manual asset preparation.

## Milestone 9 — Lightweight 3D Maturity

1. Stabilize transform hierarchy: full parent transform composition, cycle
detection, consistent scale/rotation handling, and tests.
2. Provide reliable glTF asset importing, skinned-model animation states,
materials, texture settings, and debug rendering.
3. Add scene queries and collision behaviour needed by actual lightweight 3D
games. Replace the current simple 3D helper only if real requirements prove a
dedicated physics library is necessary.
4. Add frustum culling, batching/instancing, asset lifetime management, and
profiling before adding visually ambitious renderer features.
5. Grow `minimal_3d` and `animation_3d` into reference scenes with explicit
performance budgets.

**Done when:** small exploration, third-person, or 3D puzzle games can ship on
the supported platform without promising Unity-class graphics tooling.

## Milestone 10 — Networking As A Game-Facing Layer

1. Keep transport/security code isolated. Define high-level session, message,
RPC/event, and replicated-state APIs that Lua games actually consume.
2. Integrate replication with component metadata and explicit serialization;
replicate only fields marked as safe/required.
3. Add authority ownership, spawn/despawn, interpolation, snapshots, and
network diagnostics.
4. Create a two-client headless integration test and make it repeatable in CI.
5. Use the platformer/reference game to prove the API before extending it.

**Done when:** a Lua game can host, connect, replicate selected game state, and
report useful connection/authority errors without knowing transport details.

## Milestone 11 — Editor, After The Data Model Is Stable

1. Build the editor on the same component registry, scene serializer,
validation diagnostics, prefabs, and command layer used by the CLI.
2. Deliver a small editor MVP: hierarchy, inspector, asset browser,
diagnostics, Scene/Game views, and Play/Pause/Stop from a scene snapshot.
3. Make every mutation an undoable command and keep editor state separate from
authored scene state.
4. Add transform/grid/UI editing tools only for the stable data systems above.
5. Keep CLI parity: no editor-only content that cannot be validated or reviewed
in source control.

**Done when:** the editor makes existing JSON/Lua workflows faster, without
becoming a second, hidden representation of the game.

## Continuous Requirements

For every milestone:

1. Update schemas, file-format documentation, Lua annotations/stubs, and
focused tests together with public format/API changes.
2. Run `cmake --build --preset linux-debug`, `ctest --preset linux-debug`, and
`demi validate` for each affected example.
3. Add a headless runtime smoke test for every new reference capability.
4. Keep source formatting deterministic and generated/cooked outputs outside
source directories.
5. Update the capability matrix and explicitly mark experimental features.

## Explicit Deferrals

Do not prioritize these until a shipped/reference project proves the need:

- Unity-compatible API or file formats.
- Visual scripting, shader graphs, a marketplace, or a general plugin system.
- AAA renderer features such as global illumination, terrain tooling, VFX
graphs, and high-end post-processing.
- Accounts, matchmaking, NAT traversal, backend hosting, anti-cheat, or live
service operations.
- Multiple desktop platforms before Linux packaging and testing are solid.
- More example-specific components that bypass the component registry.

## Recommended Execution Order

`0 Contract → 1 Components → 2 Prefabs → 3 UI → 4 2D foundation → 5 State /
debugging → 6 Isometric → 7 Combat → 8 Assets / shipping → 9 Lightweight 3D →
10 Networking layer → 11 Editor`

Milestones 6, 7, and 9 can be scheduled according to the next game you want to
make, but only after milestones 1–5 establish the reusable foundation.
