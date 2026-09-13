# Agent Rules For DemiEngine

DemiEngine is a Linux-first C++20 game engine for deterministic JSON authoring
and Lua gameplay. Its production focus is 2D, isometric/2.5D, UI-heavy, and
data-heavy games. Lightweight 3D is experimental. Keep changes aligned with
the actual capability matrix rather than implying Unity-scale 3D features.

## Start Here

- Inspect `git status`, the relevant diff, and recent commits before editing.
  The worktree may contain valuable user changes. Never overwrite, reformat,
  or remove unrelated work.
- Read the relevant document under `docs/` and the active section of `plan.md`.
  Update maintained documentation when a public workflow or contract changes.
- Treat examples as executable engine probes. A repeated example workaround is
  evidence that a reusable engine, schema, editor, or Lua API is missing.
- Prefer convention over configuration, but do not hide durable identity,
  ownership, security, migration, or platform requirements.

## Build And Test

- Configure: `cmake --preset linux-debug`
- Build: `cmake --build --preset linux-debug`
- Test: `ctest --preset linux-debug`
- Do not add a `-j` flag to builds. The build tool may use the machine's
  available parallelism itself.
- CLI help: `./build/linux-debug/demi --help`
- Validate a project and all reachable source:
  `./build/linux-debug/demi validate examples/minimal_3d`
- Validate a shipping target:
  `./build/linux-debug/demi validate examples/minimal_2d_android --platform android`
- Headless smoke:
  `DEMI_HEADLESS=1 ./build/linux-debug/demi run --project examples/minimal_3d/demi.project.json --max-frames 3`
- Desktop E2E: `./build/linux-debug/demi test linux --project <project>`
- Run on an attached phone:
  `./build/linux-debug/demi run android --project <project> --serial <device>`
  This reuses a current APK and builds, installs, and launches when inputs are
  newer or the APK is missing. Add `--watch` for cooked source hot reload.
- Attached Android device: `./build/linux-debug/demi test android --project <project> --serial <device>`

Run the narrowest relevant tests while iterating, then broaden validation in
proportion to risk. Never report a full green gate if unrelated or newly added
tests are failing; identify the exact failing test and whether it predates the
change.

## Architecture And Dependency Direction

Dependency flow is one-way:

1. CLI/runtime/editor entry points coordinate application services.
2. Runtime systems consume the world and narrow subsystem APIs.
3. Components own authored defaults, parsing, metadata, and dimensional domain.
4. Lua bindings adapt runtime services; domain/component code does not import
   sol2 or expose raw C++ storage.
5. SDL3, bgfx, Box2D, miniaudio, ENet, mbedTLS, and other integrations remain
   behind their owning runtime subsystem.

Important owners:

- `src/demi/assets`: manifests, registry, import/cook/package asset behavior.
- `src/demi/diagnostics` and `src/demi/schema`: shared CLI/editor diagnostics
  and validation.
- `src/demi/filesystem`: deterministic project and source discovery.
- `src/demi/runtime/app`: composition roots and loop orchestration, not domain
  algorithms.
- `src/demi/runtime/platform`: SDL lifecycle, display, native windows, input,
  clipboard, permissions, and writable platform paths.
- `src/demi/runtime/scene`: project/scene/HUD loading, composition, lifetime,
  and stable object models.
- `src/demi/runtime/render`: backend-neutral render systems and bgfx adapters.
- `src/demi/runtime/scripting`: Lua VM lifecycle and installable bindings.
- `src/editor`: authored document commands and presentation over shared runtime
  services; it must not become a second engine implementation.

Split large files by responsibility and reason to change, not arbitrary line
count. Keep coordinators thin; parsing, persistence, platform integration,
rendering, validation, and presentation should not accumulate in one class.

## Source, Generated, And Writable Data

- Human/agent-authored source stays outside `build/`, `generated/`, and
  `examples/**/generated/`.
- Do not hand-edit or commit build outputs, cooked files, APKs, AABs, generated
  shaders, Gradle intermediates, or cook caches.
- Game saves and caches use platform user-data/cache directories, never the
  application or cooked source directory.
- `scripts/stubs/demi.lua` is checked-in source for public Lua API metadata.
  Update it whenever public Lua APIs change.
- Package output may contain only the audited cooked roots described in
  `docs/shipping.md`. Do not bypass the package-content audit or copy arbitrary
  project directories into a release.

## Authored Data And Convention Over Configuration

- Every durable project, scene, HUD, prefab, asset, save, and package document
  keeps `format_version`. Never remove it as “boilerplate.”
- Use stable IDs and URI references: `scene://`, `asset://`, `prefab://`,
  `ui-prefab://`, and `script://`. Do not replace stable references with array
  positions or editor-only handles.
- Asset manifests use `*.asset.json`; authored scenes use `*.scene.json`; HUDs
  use `*.hud.json`. Generated paths never become the authored identity.
- Project scene entries may omit `path` when the conventional path can be
  inferred from the scene ID (`scene://namespace/main` becomes
  `scenes/main.scene.json`). Explicit paths remain valid.
- Prefer existing input presets (`wasd_arrows`, `confirm`, `gamepad_confirm`,
  `move_3d`) and entity presets (`static_box_3d`, `trigger_sphere_3d`,
  `prop_2d`, `character_3d`) over repeating their expanded fields. Explicit
  fields override preset values.
- HUD authoring supports an omitted root through top-level `children`, implicit
  fill containers, `dock`, `stack`, `pad`, `at`, type defaults, style defaults,
  and hex colors. Use these conventions before adding more raw anchors and
  repeated control properties.
- Safe areas protect readable/interactive HUD content by default. Set
  `respect_safe_area: false` only for intentional edge-to-edge backgrounds or
  full-screen compositions; ensure important controls remain clear of cutouts.
- The top-level project `assets` array means startup preload. It must not cause
  every project asset to load automatically. Lua can load/unload individual
  `asset://` resources or explicit asset groups.
- Omitted component fields use their canonical component defaults. Do not
  materialize every default into newly authored JSON. Preserve explicit values
  in existing user files unless the user asks for a migration.
- Keep JSON formatting, key order, compact arrays, and neighboring multiline
  style stable. Editor saves must patch source with small diffs and normalize
  float noise without reformatting the document.

Any new shorthand or preset must update parser, schema, validator, CLI inspect
output, editor behavior, documentation, and tests together. Expanded legacy
forms must remain supported according to `docs/compatibility.md`.

## Component Contract Changes

When adding or changing a component or field, update the complete contract:

- C++ value type and canonical defaults;
- component registry/reflection metadata;
- JSON parser and schema validation;
- scene/prefab composition and asset/reference discovery;
- editor Inspector/add-component behavior;
- Lua creation/bindings only when gameplay needs them;
- Lua stubs when public;
- round-trip, validation, and focused runtime tests.

Reflection metadata should be the single source for serialization, Inspector
controls, schemas, docs, Lua stubs, and default comparison as those paths are
consolidated. Do not create a parallel editor-only component model.

## Lua Gameplay

- Use `on_create`, `on_start`, `on_update`, `on_fixed_update`, and `on_destroy`.
- Prefer current concise APIs such as `Input.pressed`, `Input.down`,
  `Input.value`, and normalized `Input.vector`; compatibility aliases may still
  exist but new examples should teach the concise names.
- `Input.axis(negative, positive)` returns positive minus negative.
- Use `@demi_component` and assignment-based `@demi_property` annotations for
  editor-visible game-specific behavior. Display name, category, description,
  and most property metadata are optional and inferred. Do not add a second
  JSON metadata block or synthetic script-component ID.
- Keep Lua APIs high-level and stable (`Entity`, `Transform2D`, `Transform3D`,
  `Hud`, `Assets`, `Scene`, `NetworkSession`). Do not expose raw component
  storage, renderer handles, or backend types.
- Keep gameplay rules in Lua or reusable packages, not hardcoded in the engine.
  Engine code should provide general mechanisms, deterministic state, and
  serialization boundaries.
- Preserve Lua array-like tables as numeric arrays across JSON/network bridges.
- If a public API changes, update examples, templates, docs, tests, and
  `scripts/stubs/demi.lua` in the same change.

## Scene, Transform, Physics, And Rendering

- `Transform2D.parent` and `Transform3D.parent` are stable entity IDs, never
  pointers. Resolve parent transforms through shared hierarchy helpers before
  rendering, physics, camera, or editor calculations.
- Physics behavior and debug drawing must use the authored collider shape.
  Never represent sphere/capsule gameplay as a box merely for convenience.
- Scripted dynamic movement must continue respecting static collision and
  deterministic fixed-step behavior. Add regression tests for movement,
  contact, hierarchy, or interpolation changes.
- Visible Linux and Android rendering is bgfx/Vulkan-first through SDL3. Do not
  reintroduce raylib assumptions or call bgfx directly from gameplay/editor
  domain code.
- VSync presentation and CPU frame limiting are mutually coordinated. Do not
  add an extra sleep on a compositor-paced swapchain. Android frame-rate hints,
  surface recreation, pause/resume, and native-window rebinding must remain
  lifecycle-safe.
- Measure performance changes using the runtime profiler and a repeatable scene.
  FPS alone is not enough; retain frame-time and subsystem evidence.

## Networking And Security

- Network contracts, authority, ownership, and allowed message/component data
  are runtime policy, not Lua convention or client trust.
- Keep transport/security code behind `NetworkSystem`, TLS/DTLS services, and
  validated session/message boundaries. Do not log secrets, signing material,
  certificates, or unredacted credentials.
- Preserve deterministic, validated payload shapes. Array-like Lua payloads
  must round-trip as arrays.
- Tests and game code can consume the same event queues. Prefer polled state in
  E2E tests when gameplay legitimately drains connection events.
- New prediction, reconciliation, or replication work must preserve server
  authority and add deterministic tests for correction, ownership, disconnect,
  and late-join behavior.

## Editor Rules

- Scene and HUD editing are integrated workspace documents. Clicking a
  registered scene source switches the active scene; clicking a HUD source
  opens the HUD stage using the same hierarchy, viewport, selection, Inspector,
  Undo/Redo, Save, diagnostics, and recovery paths.
- Do not recreate the removed modal JSON-only HUD editor or maintain a second
  unsynchronized HUD preview model.
- Scene mutations are command-like, reversible, validated, and keyed by stable
  IDs. Failed preview rebuilds restore the document and history.
- The editor must use the same loaders, metadata, diagnostics, importer,
  `BuildService`, cook, and package paths as the CLI/runtime.
- Play mode owns an isolated runtime world. Runtime hierarchy/debug/profiler
  state must not silently mutate authored scene data.
- Keep editor presentation in ImGui adapters. Runtime, document, validation,
  and command code must remain UI-free and testable without opening a window.
- Docking uses the pinned official Dear ImGui docking branch in the main native
  window only. `EditorDockingWorkspace` owns the dock graph/default layout;
  panels use stable window IDs and derive rendering/input rectangles from their
  live content region rather than global screen coordinates.
- Authored Scene/HUD and embedded Game views render to GPU targets displayed as
  ImGui images. Do not return docked viewports to direct backbuffer regions:
  dock-node backgrounds are composited later and will cover them.
- Dock layout and panel visibility are per-user state below the platform data
  directory. They must never enter a project, generated content, packages,
  authored undo history, or runtime state.
- Preserve source formatting and atomic/conflict-aware save behavior. Never
  trade small reviewable diffs for convenient whole-document serialization.

## Assets, Packages, And Shipping

- Asset IDs are stable; groups only batch loading/unloading and do not change
  individual asset semantics or force global preload.
- Import/reimport goes through `AssetImporter`; cooking records deterministic
  hashes and dependencies. Update public asset Lua stubs when APIs change.
- Package dependencies use manifests/locks and declared visibility. Do not read
  another package's private module path directly.
- Validate the exact target before packaging. Android lacks some optional
  desktop capabilities; do not silence platform capability diagnostics.
- Release signing values come only from environment/CI secret references and
  must never enter source, command output, reports, or packaged content.
- Packaging is transactional. A failed/cancelled build must leave the previous
  artifact intact and must not publish a partial staging directory.

## End-To-End Tests

- Project E2E tests live at `scripts/tests/e2e.lua` and run through the real
  runtime using the `Test` API.
- Prefer stable HUD node IDs (`Test.touch`) and scene IDs
  (`Test.expect_scene`) over device coordinates so tests survive resolution,
  orientation, DPI, and safe-area changes.
- Keep tests deterministic, bounded by explicit waits/timeouts, and independent
  where possible. The harness must always emit a final pass/fail summary.
- Android qualification uses a connected physical device through adb; do not
  assume an emulator is available.

## Definition Of Done

- The requested behavior is implemented at the correct shared boundary.
- Authored and generated data remain deterministic and separated.
- Schemas, validators, editor behavior, Lua bindings/stubs, examples, and docs
  agree with any changed public contract.
- Relevant focused tests pass; affected example projects validate; platform
  changes receive an appropriate desktop/headless/device smoke test.
- `git diff --check` is clean, source is formatted consistently, and unrelated
  user changes remain untouched.
