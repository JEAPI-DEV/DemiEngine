# Closing DemiEngine's Game-Development Gaps

## Purpose

DemiEngine should let a developer build ordinary 2D, isometric, lightweight
3D, UI-heavy, and networked games by adding project data, Lua scripts, and
assets. A new game should not require edits under `src/demi` merely because it
needs a standard component property, runtime prefab, touch gesture, particle
effect, collision callback, audio bus, or replicated value.

This roadmap targets the practical freedom associated with Unity's common game
workflow. It does **not** target feature-for-feature Unity parity, a high-end
render pipeline, terrain authoring, visual scripting, or an editor-only asset
format. DemiEngine's deterministic text formats, CLI validation, and
Linux/Android focus remain product constraints.

No phase is complete merely because an API exists. Each phase must prove that
games can use the capability without a game-specific C++ patch.

## Audit Summary

The engine has a good set of individual systems, but the reusable layer between
those systems and game code is incomplete.

| Existing evidence | Consequence for game developers |
|---|---|
| Component descriptors drive authored parsing, validation, schemas, replication flags, and editor metadata. | This is the right source of truth and should be extended rather than replaced. |
| `luaParseEntitySpec` still names and constructs individual component types. | Runtime spawning can lag behind scene authoring, and a component addition can require another binding change. |
| Lua exposes narrow setters such as `Entity.set_sprite_color` and `Sprite2D.set_size`. | Every ordinary property that was not predicted by the engine requires C++ work. |
| `Entity.create` replaces an existing entity immediately and `destroy` erases directly from the world. | Mutations are not expressed as lifecycle-safe world commands, and dynamically added scripts/components have weak lifecycle guarantees. |
| `Scene` exposes only `load(scene_id)`. | Loading screens, additive scenes, persistent objects, reloads, and scene-independent services must be improvised. |
| `InputState` models keys, mouse buttons, one pointer position, and text. Android touch is converted to a left mouse button. | Multi-touch, gestures, gamepad axes, rebinding, safe areas, and mobile controls are not first-class. |
| 2D physics is Box2D-backed and useful, while 3D physics is collision-aware movement and spatial queries rather than a complete simulation. | Standard 2D games are close; standard 3D character, trigger, rigidbody, and joint behavior still causes engine changes. |
| Rendering supports sprites, tilemaps, simple shapes, glTF models, one directional-light concept, and HUDs. | Materials, particles, render targets, multiple cameras, common lights, and post effects are missing reusable presentation primitives. |
| UI has a tree layout, anchors, themes, localization, and common controls, but runtime structure is mostly static. | Dynamic lists, generated screens, drag/drop, value-change events, safe areas, and world-space UI need script workarounds or engine edits. |
| `NetworkSession` provides transport-independent identity, authority, snapshots, and events. | Games still hand-build replicated gameplay state, late-join repair, RPC conventions, and disconnect bookkeeping. |
| Assets are validated, imported, packaged, and cooked, but render/audio resources are largely loaded as a startup set. | Larger games lack explicit load groups, streaming, unload, memory ownership, and loading progress. |
| `src/editor/main.cpp` is a placeholder. | Authors must edit JSON directly even for hierarchy, transforms, colliders, prefabs, and animation graphs. |
| Linux packaging works and Android APK packaging works, but Android remains experimental. | Release signing, app metadata, lifecycle, permissions, orientation, storage, and device validation are not a finished product path. |

The primary issue is therefore not “missing Unity features.” It is that the
same component description does not yet power the complete component
lifecycle:

```text
author -> validate -> load -> create -> query -> mutate -> inspect
       -> serialize -> replicate -> undo -> reload
```

Until that chain is unified, adding more components will continue to create
more maintenance points.

## Roadmap Rules

Every phase follows these rules:

1. Reflection metadata is the source of truth for fields, defaults,
   validation, serialization, Lua policy, replication, and inspection.
2. Game rules remain in Lua. C++ owns reusable simulation, platform, asset,
   rendering, and serialization behavior.
3. Runtime world changes go through commands or mutation queues rather than
   invalidating active system iteration.
4. Linux and Android use the same project, scene, prefab, save, and script
   formats.
5. Public Lua changes update checked-in stubs, generated stubs, documentation,
   script checks, and contract tests together.
6. Each new component updates scene loading, validation, schemas, round-trip
   tests, Lua exposure where useful, debug presentation, and replication policy
   where applicable.
7. An example is a capability probe, not the permanent owner of reusable
   engine behavior.
8. Existing lower-level APIs may remain for tools, but examples should use the
   stable game-facing layer.

## Phase 0 — Capability Baseline and “No Engine Edit” Gates

This phase makes progress measurable before more implementation begins.

**Status: complete.** The live Lua VM and component registry generate the
capability manifest; a checked compatibility baseline and seven reference-game
gates are enforced through CTest. Known missing behavior is tracked in
`capabilities/reference_gates.json`.

### Deliverables

- Add an automated capability manifest generated from component and binding
  metadata.
- Add a public API compatibility report to CI so accidental removals or type
  changes are visible.
- Define reference game gates:
  - a 2D platformer;
  - a top-down action game;
  - an isometric strategy/tower-defense game;
  - a UI-heavy save/load game;
  - a lightweight 3D character scene;
  - a two-client networked game;
  - the same touch-driven action game packaged for Android.
- For each gate, record every required engine edit as a capability-gap issue
  instead of embedding an example-only workaround.
- Add a test that fails if an example imports private runtime internals or
  relies on generated/editor-only state.

### Done when

- All gates build and validate through CI.
- A new gate can be added entirely under `examples/`.
- Capability status is generated or test-backed rather than maintained only by
  prose.

## Phase 1 — Reflection-Driven Runtime Object Model

This is the highest-priority phase. It removes the most common reason games
need engine edits.

**Status: complete.** Runtime and authored entity construction now share the
component registry, mutations are deferred through `WorldCommandBuffer`, and
Lua exposes stable generic entity/component, query, enable, and hierarchy
operations. Component metadata drives validation, schemas, capability
contracts, and generated Lua component specs; dynamic `LuaScript` lifecycle
and mutation ordering are covered by regression tests.

### Ownership

- `ComponentRegistry` owns construction, field access, validation, and
  serialization contracts.
- A `WorldCommandBuffer` owns deferred create/destroy/add/remove/enable
  mutations.
- Lua bindings adapt those contracts; they do not contain another component
  parser.

### Deliverables

- Replace component-specific runtime entity parsing with registry-driven
  construction.
- Extend field descriptors with:
  - readable/writable Lua policy;
  - nullable/optional information;
  - numeric maximum and range metadata;
  - asset/entity/prefab reference kinds;
  - array element and nested-object schemas;
  - restart-required or read-only runtime policy.
- Add stable game-facing APIs:

  ```lua
  Entity.exists(id)
  Entity.create(spec)
  Entity.clone(source_id, new_id)
  Entity.set_enabled(id, enabled)
  Entity.is_enabled(id)
  Entity.add_component(id, component_name, values)
  Entity.remove_component(id, component_name)
  Entity.has_component(id, component_name)
  Entity.get(id, component_name, field)
  Entity.set(id, component_name, field, value)
  Entity.query({ all = {"Transform2D", "Sprite"}, tags = {"enemy"} })
  ```

- Add authored and runtime `enabled`, `tags`, and gameplay `layer` data.
- Add parent/child APIs with cycle checking and explicit world/local transform
  operations for both dimensions.
- Make entity IDs stable handles with clear behavior after destruction.
- Queue runtime mutations until safe synchronization points.
- Define lifecycle behavior for dynamically added and removed `LuaScript`
  components, including `on_create`, `on_start`, and `on_destroy`.
- Make replacement of an existing entity explicit; ordinary create must reject
  duplicate IDs.
- Generate Lua field types and component specs from metadata.
- Remove bespoke setters when the generic API fully replaces them; retain
  focused high-frequency services such as `Transform` and `Rigidbody2D` where
  they improve clarity or performance.

### Done when

- Adding a metadata-complete component does not require edits to a Lua entity
  parser, schema switch, or generic inspector.
- Every authored component can be created at runtime from the same JSON-shaped
  data.
- Runtime changes are validated with the same diagnostics as authored scenes.
- Dynamic scripts receive deterministic lifecycle callbacks.
- Unit tests cover invalid fields, duplicate IDs, mutation ordering,
  enable/disable, parenting, and component add/remove.

## Phase 2 — Runtime Prefabs, Scene Flow, and Resource Lifetimes

Unity-like productivity depends more on reusable composition and predictable
lifetimes than on a large component list.

**Status: complete.** `RuntimePrefabService` reuses authored prefab expansion
and registry-driven entity construction, including nested stable IDs,
overrides, placement, and optional reset-on-reuse pooling. `SceneFlow` owns
asynchronous preparation, additive/full activation, unload/reload, deterministic
ID diagnostics, persistence, and scene-owned entity/UI/resource groups. Lua
exposes these services together with scaled/unscaled clocks, pause, fixed time,
frame count, and application focus/suspend state and events. Headless tests
cover prefab pooling, scene preparation/activation/unload, persistent entities,
duplicate rejection, script lifecycles, and runtime clocks.
The lifetime failure suite additionally injects partial resource failures,
script exceptions, teardown-time commands, shared assets, cancelled
preparations, pooled scene assets, and repeated transitions. The `linux-asan`
preset runs the stress loop under ASan/LSan.

### Deliverables

- Add game-facing prefab APIs:

  ```lua
  Prefab.instantiate("prefab://enemies/grunt", {
    id = "grunt_42",
    position = {4, 2},
    overrides = { ... }
  })
  Prefab.release(entity_id)
  ```

- Apply the existing prefab expansion, override, cycle-diagnostic, and
  validation path at runtime.
- Support nested prefab instances with stable instance/local IDs.
- Add a built-in entity pool that preserves lifecycle and component reset
  rules. Pooling must be optional; ordinary instantiate/destroy remains valid.
- Expand `Scene` with:
  - load, reload, unload, and additive load;
  - persistent entities or a persistent service scene;
  - asynchronous preparation with progress;
  - explicit activation after preparation;
  - scene-loaded, scene-unloading, and active-scene events;
  - deterministic failure diagnostics.
- Add pause, time scale, unscaled time, frame count, fixed time, and application
  focus/suspend events.
- Introduce asset/resource lifetime groups owned by scenes and persistent
  services instead of loading every resource for the process lifetime.

### Done when

- A wave of enemies, bullets, pickups, and effects is instantiated from
  prefabs without repeating component tables in Lua.
- A loading screen can prepare and activate another scene without blocking the
  presentation loop.
- Additive UI/gameplay scenes unload without leaving scripts, audio, network
  entities, or assets alive.
- Scene and prefab behavior is covered by headless lifecycle tests.

## Phase 3 — Cross-Platform Input and Application Services

Input should describe player intent and device state without pretending every
device is a mouse.

**Status: complete.** Typed actions now resolve keyboard, mouse, four
independently assigned gamepads, and touch-backed virtual controls through
contexts and per-player bindings. Runtime rebinding persists below the
platform user-data path. Stable multi-touch snapshots, gesture recognition,
per-pointer UI capture, safe-area layout, DPI/UI scale, orientation requests,
keyboard, clipboard, lifecycle events, and writable paths are exposed through
game-facing services. Replay format 2 records action, gamepad, touch, and
virtual state while retaining format 1 compatibility. The shared Linux/Android
shooter uses the same action script for all devices.

### Ownership

- `InputActionResolver` owns authored intent resolution and processors.
- `InputRebinding` owns transactional binding override persistence.
- `TouchGestureRecognizer` owns deterministic gesture state.
- `UiInteractionController` owns pointer-ID capture; HUD virtual controls only
  publish named input values.
- `ApplicationServices` owns platform state and paths; runtime adapters feed
  it Linux/raylib and Android lifecycle/display data.

### Deliverables

- Extend input actions with:
  - button, one-dimensional axis, and two-dimensional vector action types;
  - gamepad buttons, triggers, sticks, dead zones, inversion, and processors;
  - composite bindings and multiple local players;
  - action maps/contexts such as gameplay, menu, vehicle, and chat;
  - runtime rebinding with save/load;
  - pressed, released, held, value, and device-source information.
- Add first-class touch input:
  - stable pointer IDs;
  - began/moved/ended/cancelled phases;
  - position, delta, pressure when available, and touch count;
  - tap, double-tap, long-press, drag, pinch, and rotate recognizers;
  - reusable virtual stick and virtual button UI controls.
- Preserve pointer-to-UI capture per pointer rather than through one global
  mouse flag.
- Add platform/application data:
  - safe-area insets;
  - logical DPI and UI scale;
  - orientation and orientation requests;
  - keyboard/IME visibility;
  - clipboard;
  - focus, minimize, suspend, resume, and low-memory events;
  - writable user-data/cache paths.
- Add deterministic replay representation for the new action, gamepad, and
  touch states.

### Done when

- The same action script supports keyboard/mouse, gamepad, and Android touch.
- Controls can be rebound without editing the project file.
- A two-player local game can assign separate gamepads.
- Notched and differently sized Android screens keep controls inside the safe
  area.
- Input replay remains deterministic across Linux headless tests.

## Phase 4 — Production 2D Gameplay Foundation

The objective is to make common 2D genres possible without another physics or
render binding for each game.

**Status: complete.** The Box2D-backed runtime now provides contact lifecycle
events, rich queries, production body controls, common collider/material/joint
shapes, and CCD regression coverage. Shared Lua controllers cover platform,
top-down, and navigation-driven movement. Sprites support source modes,
nine-slice, masking, materials, and runtime ordering. Mutable multi-tileset
tilemaps carry animation, object, collision, and navigation metadata; tile
edits refresh collision and navigation. The platformer, shooter, fighting, and
isometric probes use the shared game-facing layer on Linux, with the shooter
also packaged for Android.

### Deliverables

- Complete 2D physics gameplay APIs:
  - collision and trigger enter/stay/exit events;
  - rich overlap/raycast results with point, normal, fraction, layer, and
    entity;
  - box, circle, capsule, polygon, edge, and chain colliders;
  - collision materials for friction, restitution, and density;
  - kinematic move/slide operations;
  - continuous collision for fast bodies;
  - forces, torque, angular velocity, constraints, sleeping, and body enable;
  - revolute, prismatic, weld, rope, and motor joints.
- Ship reusable 2D character-controller components/helpers for platform,
  top-down, and click-to-move control without embedding genre rules.
- Add sprite renderer features used by ordinary games:
  - nine-slice sprites;
  - sprite masks/clipping;
  - per-sprite material reference;
  - normalized and pixel source rectangles;
  - runtime layer/order/material mutation.
- Expand tilemaps with runtime tile get/set, chunk dirtying, object layers,
  animated tiles, multiple tilesets, and nav/collision metadata.
- Add a general 2D navigation grid with path requests, dynamic blockers,
  costs, and agents. Keep isometric projections as adapters over the shared
  navigation model.

### Done when

- Platformer, top-down shooter, puzzle, racing, and isometric reference games
  share engine physics/query APIs instead of their own collision loops.
- Fast projectiles do not tunnel in the regression suite.
- Runtime tile edits update rendering, collision, and navigation together.
- Debug collider and navigation rendering matches simulation behavior.

## Phase 5 — Complete Lightweight 3D Gameplay Foundation

This phase closes the gap between “renders a 3D scene” and “supports a standard
small 3D game.” It does not promise high-end Unity rendering.

**Status: complete.** Jolt 5.6.0 now sits behind `PhysicsWorld3D`; explicit
3D colliders, fixed-step bodies, contacts, narrow-phase queries, a capsule
controller, transform/camera helpers, validation, debug rendering, Lua APIs,
and the migrated `minimal_3d` reference probe are covered by focused
regressions. See `docs/3d-gameplay.md`.

### Deliverables

- Select and document a maintained 3D physics backend behind a
  `PhysicsWorld3D` boundary.
- Add simulated static, kinematic, and dynamic bodies with:
  - gravity, mass, damping, forces, impulses, velocity, constraints, sleep;
  - collision layers and masks;
  - collision/trigger enter/stay/exit;
  - box, sphere, capsule, convex, and static triangle-mesh colliders;
  - sweeps, shape casts, overlaps, and raycasts with rich hit results;
  - fixed-step interpolation.
- Define valid dynamic/static mesh-collider rules and make validation reject
  unsafe combinations.
- Add a reusable capsule character controller with slope, step, grounding,
  slide, and moving-platform behavior.
- Add common joints and configurable constraints only after rigidbody behavior
  is stable.
- Expose world/local transform directions, look-at, screen/world conversion,
  and camera ray generation.
- Keep generated collider detail as an import concern, while runtime collider
  behavior remains determined by explicit collider components.

### Done when

- A third-person or first-person lightweight 3D example uses only public APIs
  for movement, grounding, triggers, pickups, moving platforms, and projectiles.
- 3D simulation and debug shapes agree.
- Physics behavior is deterministic enough for replay tests within documented
  tolerances.
- Android and Linux run the same 3D scene with the same authored colliders.

## Phase 6 — Materials, Cameras, Lighting, Particles, and Render Effects

Presentation features should be data-driven assets/components rather than
branches added to renderer files for each example.

### Ownership

- Material assets own shader, texture slots, render state, and parameters.
- Render pipelines own pass order and targets.
- Renderer adapters consume component/material data; game scripts do not call
  raylib.

### Deliverables

- Add versioned `Material` and `Shader` assets with validated parameters,
  texture slots, blend/cull/depth state, and platform fallbacks.
- Add material instances/property blocks for per-entity values without
  duplicating assets.
- Add point and spot lights, ambient/environment settings, and a bounded,
  configurable shadow implementation suitable for the lightweight target.
- Add multiple cameras, viewport rectangles, render masks, camera priority,
  clear modes, and render-to-texture.
- Add 2D and 3D particle components with:
  - emission shape/rate/bursts;
  - lifetime, velocity, gravity, size, rotation, and color curves;
  - texture/material and sorting settings;
  - deterministic seeds;
  - pooling and mobile budgets.
- Add a small post-process stack: color adjustment, vignette, bloom, and
  screen fade. Do not add a shader graph.
- Add world-space text and world-space UI targets.
- Add render statistics for batches, triangles, particles, lights, shadow
  passes, and render-target memory.

### Done when

- A game can add muzzle flashes, impacts, weather, pickups, damage flashes,
  minimaps, split-screen, and simple day/night lighting without modifying a
  renderer.
- supported material/shader features validation Linux and Android.
- Render resource ownership and reload are leak-tested.

## Phase 7 — Animation and Audio for Finished Games

The current state machine is a useful base. This phase adds the transitions,
mixing, and runtime control needed for production presentation.

### Animation deliverables

- Cross-fades and blend durations.
- One-dimensional and two-dimensional blend spaces.
- Animation layers, masks, additive playback, and per-layer weights.
- Root-motion extraction with an explicit gameplay opt-in.
- Runtime clip speed, normalized time, event subscription, and transition
  inspection.
- Import validation for clip names, skeleton compatibility, and missing state
  references.
- 2D atlas/sprite-sheet import helpers so clips need not be handwritten.
- Animation preview data consumable by both CLI tools and the future editor.

### Audio deliverables

- Mixer buses/groups with volume, mute, pause, and routing.
- Music, SFX, voice, and UI defaults.
- Spatial 2D/3D audio, attenuation curves, pan, pitch, and Doppler opt-in.
- Fades, cross-fades, scheduled playback, and mixer snapshots.
- Streaming music/ambience rather than loading every clip fully.
- Concurrency limits and voice stealing.
- Device interruption and Android suspend/resume handling.

### Done when

- Locomotion blends, attacks cross-fade, footsteps use animation events, and
  root motion can be enabled without renderer-specific code.
- Music transitions and gameplay ducking use mixer data rather than per-frame
  Lua volume loops.
- Audio and animation state survive pause/resume according to explicit policy.

## Phase 8 — Dynamic Runtime UI and Accessibility

The tree/anchor foundation should become a complete game UI layer, not a
collection of static HUD mutations.

### Deliverables

- Runtime create, clone, remove, reparent, and query for UI nodes using the same
  validated node schema as HUD files.
- Reusable UI prefabs/templates and data-driven list/grid item generation.
- Value-changed, focus, submit, cancel, pointer enter/exit, press/release,
  drag/drop, and scroll events with typed payloads.
- Two-way bindings for simple view-model values without moving gameplay rules
  into the UI system.
- Safe-area containers and DPI-aware minimum touch target sizing.
- Text wrapping, alignment, truncation, rich spans, fallback fonts, Unicode,
  shaping, and IME composition.
- Navigation groups, explicit neighbor overrides, controller glyph switching,
  and focus restoration.
- Accessibility roles, labels, values, contrast diagnostics, reduced-motion
  settings, and screen-reader adapter boundaries where platforms permit.
- World-space canvases and multiple UI canvases with deterministic ordering.
- UI animation integrated with node lifetime so destroyed nodes cannot leave
  active tweens/callbacks.

### Done when

- Inventory, settings, lobby, scoreboard, dialogue, and virtual controls are
  built from reusable UI data without hard-coded renderer nodes.
- Dynamic UI creation uses no immediate-mode compatibility functions.
- Keyboard, gamepad, mouse, touch, and text input can complete the showcase.
- Layout tests cover phone aspect ratios, safe areas, desktop resizing, and
  localization expansion.

## Phase 9 — Game-Facing Networking Completion

Networking should provide common multiplayer building blocks while leaving
game rules and authority decisions to the game.

### Deliverables

- Typed peer-connected and peer-disconnected events containing stable peer IDs
  and reasons.
- Automatic late-join spawn/state replay for all active replicated entities.
- Declarative replicated component fields driven entirely by metadata,
  including change detection and configurable rates/reliability.
- Game-facing RPC/event declarations with sender, target, authority, payload
  validation, and rate limits.
- Replicated spawn from prefab IDs rather than a single session-wide remote
  prefab.
- Explicit ownership transfer, despawn, reconnect, and session reset
  lifecycles.
- Host-side input command queues and snapshots suitable for authoritative
  action games.
- Optional prediction, reconciliation, interpolation, and lag-compensated
  query helpers with documented limits.
- Lobby/session state, ready state, capacity, metadata, and scene transition
  coordination.
- Dedicated server packaging and headless server configuration.
- Android network lifecycle, background disconnect, and local-network
  diagnostics.
- Keep account services, global matchmaking infrastructure, and anti-cheat
  services outside the engine core.

### Done when

- The FFA shooter no longer manually invents roster synchronization,
  late-join repair, or generic combat-state transport.
- A joining client receives every relevant entity and current session value
  without application resend code.
- Disconnect tests identify and remove exactly the departing peer's state.
- Host-authoritative and dedicated-server examples pass multi-client CI tests.

## Phase 10 — Asset Workflow, Streaming, and Extensibility

Games should add content types and manage larger content sets without editing
central switches.

### Deliverables

- A registered importer interface with discovery, versioning, settings schema,
  dependencies, generated outputs, and platform variants.
- Asset-type handlers for textures, fonts, audio, materials, shaders, meshes,
  animations, tilemaps, prefabs, and arbitrary validated data.
- Addressable asset groups or bundles with preload, asynchronous load, unload,
  reference tracking, progress, and memory diagnostics.
- Per-platform import overrides and compression settings.
- Texture atlasing and sprite metadata generation.
- Font atlas generation with declared ranges and fallback chains.
- Incremental cooking based on dependency hashes.
- Hot reload through the same registry/resource ownership path used by normal
  loading.
- A stable extension registration boundary for components, importers, Lua
  binding modules, validation rules, and future editor panels.
- Keep third-party/native game extensions out of `src/demi` when they can be a
  separately registered module.

### Done when

- Adding an importer or optional component module does not require editing a
  central dispatch switch.
- A scene can preload a declared group, display progress, activate, and unload
  it with memory returning to its budget.
- Cook output includes only dependency-complete reachable content.
- Hot reload cannot create duplicate GPU/audio/script resources.

## Phase 11 — Functional Editor Built on Runtime Contracts

The editor is valuable only after the preceding runtime contracts are stable.
It must remain a client of the same formats and diagnostics as the CLI.

### Deliverables

- Project browser and asset import/reimport status.
- Scene hierarchy with create, rename, parent, enable, duplicate, and delete.
- Reflection-generated component inspector with validated edits.
- 2D/3D scene viewport, selection, transform gizmos, collider visualization,
  camera preview, grid/snapping, and play-from-here.
- Prefab create/open/apply/revert/override workflow.
- UI hierarchy, anchor/layout editing, safe-area preview, and localization
  preview.
- Animation state-machine and material inspectors; visual graph editing is
  optional and must serialize to the same deterministic data.
- Play mode with an explicit edit/play state boundary.
- Undo/redo through serializable commands, not private editor mutations.
- Console, diagnostics, profiler, network diagnostics, and input inspection.
- Save-on-command and deterministic formatting; no hidden scene database.

### Done when

- An editor-created project validates and produces the same expanded scene as
  an equivalent hand-authored project.
- Every editor mutation maps to a tested command and supports undo/redo.
- The editor never bypasses component, prefab, asset, or schema validation.
- A small game can be assembled, played, diagnosed, and packaged without
  manually editing JSON, while manual JSON editing remains fully supported.

## Phase 12 — Shipping-Grade Linux and Android

Packaging must cover the ordinary tasks between “debug build runs” and
“developer can ship it.”

### Deliverables

- Project build settings for application ID, executable name, version,
  orientation, icon, splash screen, window defaults, and required permissions.
- Android debug and release variants, keystore/signing inputs, ABI selection,
  minimum/target SDK policy, and reproducible Gradle staging.
- Android lifecycle integration for suspend/resume, audio focus, surface
  recreation, safe areas, IME, clipboard, storage, back navigation, and
  low-memory notification.
- Runtime permission request/status APIs for permissions the project declares.
- Linux desktop file, icon, application data paths, and release bundle
  metadata.
- Platform capability validation before building.
- Automated device/emulator smoke tests for scene load, textures, fonts,
  touch, audio, saves, networking, suspend/resume, and orientation.
- Crash logs and structured startup diagnostics available from packaged builds.

### Done when

- One project produces a runnable Linux release bundle and signed Android
  release APK/AAB from documented CI commands.
- Platform-specific requirements live in project/build data, not modified
  engine templates.
- Android device tests cover the rendering and asset differences that
  previously appeared only after manual installation.

## Phase 13 — Performance, Diagnostics, and Stability Contracts

Optimization tools should make normal game limits visible before developers
respond with game-specific engine patches.

### Deliverables

- Hierarchical CPU frame profiler and render-pass/GPU timing where supported.
- Allocation, Lua GC, asset memory, audio voice, network bandwidth, physics,
  animation, particle, and UI counters.
- Stable entity/system diagnostics queryable by the editor and CLI.
- Configurable warnings for excessive entities, contacts, particles, lights,
  draw calls, asset residency, Lua time, or network traffic.
- Object pools and batch APIs only for measured hot paths.
- Long-running soak tests for create/destroy, scene reload, asset reload,
  connect/disconnect, Android lifecycle, and save migration.
- Public API deprecation/version policy with migration diagnostics.
- Release-mode assertions and structured crash context without leaking private
  data.

### Done when

- Performance budgets can fail CI using measured counters rather than only
  prose configuration.
- Reference games meet documented Linux and Android budgets.
- Repeated scene/network/resource lifecycles show no unbounded growth.
- A developer can determine whether a problem is script, physics, rendering,
  asset, UI, or network-bound without adding ad hoc engine logging.

## Dependency Order and Practical Releases

The numbered order is intentional:

```text
Phase 0 measurement
    -> Phase 1 runtime object model
        -> Phase 2 prefab/scene lifetimes
            -> Phase 3 platform input
            -> Phase 4 production 2D
            -> Phase 5 lightweight 3D
                -> Phase 6 rendering/VFX
                -> Phase 7 animation/audio
            -> Phase 8 runtime UI
            -> Phase 9 networking
            -> Phase 10 assets/extensions
                -> Phase 11 editor
                -> Phase 12 shipping
                    -> Phase 13 stability
```

Recommended product checkpoints:

| Checkpoint | Included phases | Developer outcome |
|---|---:|---|
| **Game API Foundation** | 0–3 | Games create, configure, compose, load, and control ordinary objects without C++ edits. |
| **2D Production Preview** | 4, 6–8 | Most small 2D/isometric/UI-heavy games can reach a polished vertical slice. |
| **Lightweight 3D Preview** | 5–8 | Small 3D action, exploration, puzzle, and voxel games have complete gameplay primitives. |
| **Multiplayer Preview** | 9 | Common host-authoritative games stop rebuilding session plumbing. |
| **Content Production** | 10–11 | Assets and scenes can be extended and authored through stable tools. |
| **Release Candidate** | 12–13 | Linux and Android projects can be packaged, profiled, tested, and shipped. |

Phases 4 and 5 can run in parallel after Phase 3. Phases 6 through 10 can also
be developed in parallel where their dependencies on Phases 1 and 2 are
already satisfied. The editor should not invent replacement data models while
those contracts are still changing.

## Explicitly Deferred

These capabilities are not required to solve the recurring “standard game
needs an engine edit” problem:

- photorealistic/high-end render pipelines;
- large open-world terrain and foliage authoring;
- shader graphs;
- visual scripting;
- cinematic timeline parity with Unity;
- full skeletal retargeting suite;
- built-in account, commerce, matchmaking, or live-operations backend;
- console platform support;
- arbitrary native plugin ABI stability before the component/asset extension
  boundary is proven in-tree;
- DOTS/ECS-scale data-oriented framework replacement.

They should be reconsidered only after reference games demonstrate a real
need, with performance or workflow evidence.

## Definition of Overall Success

This roadmap succeeds when:

- a developer can create a new standard game using project JSON, scene/HUD
  data, prefabs, assets, and Lua only;
- ordinary component fields can be authored, spawned, read, changed,
  serialized, replicated, and inspected through one metadata contract;
- Linux and Android share gameplay code and data;
- examples stop accumulating bespoke versions of engine services;
- new engine work is driven by reusable capability gaps rather than by the
  latest example's private implementation;
- the functional editor, CLI, runtime, and tests all consume the same source of
  truth.
