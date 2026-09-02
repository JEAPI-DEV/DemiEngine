# CLI Contract

The `demi` CLI is the automation interface for humans, scripts, CI, and AI agents.

## Commands

- `demi --help`: print command help.
- `demi version`: print engine version.
- `demi validate [path]`: validate a project, scene, save, or directory.
  Pass `--platform linux|linux_server|android` to also cross-check reachable
  project features and declared Android permissions against what the target
  platform's packaged runtime supports (see [Shipping](shipping.md)).
- `demi schema export`: list schema files available in `schemas/`.
- `demi scene list <project>`: list scene references from a project file.
- `demi scene inspect <scene>`: validate and summarize a scene file.
- `demi scene diff <old> <new>`: print a deterministic structural scene diff.
- `demi hud inspect <hud.json> [--safe-area l,t,r,b] [--reveal-hidden]
  [--format text|json]`: report the canvas size and every node's resolved
  rectangle using the runtime layout engine. `--safe-area` applies insets in
  canvas units the way the runtime does on device, and `--reveal-hidden` also
  resolves nodes inside hidden containers. Automation such as
  `demi test android` uses this to tap interactive nodes without hardcoding
  screen coordinates.
- `demi asset inspect <asset>`: validate and summarize an asset manifest.
  Model assets additionally expose normalized nodes, meshes, materials,
  textures, skeletons, animations, bounds, and stable JSON output.
- `demi asset deps <asset>`: list source-file dependencies for an asset manifest.
- `demi asset import <source> --project <project> --id asset://id`: copy a
  production source into the project, select its importer, generate a cached
  output, and write a complete manifest.
- `demi asset reimport <asset>`: refresh generated output, source hash, and
  importer version after a source change.
- `demi asset register-generated <source> --project <project> --id asset://id`:
  create or refresh a manifest beside a generated source under the project's
  `assets/` directory. Importer selection, source hashing, and manifest metadata
  remain owned by the engine instead of build scripts.
- `demi asset collider <model.asset.json> --project <project> --id
  asset://colliders/id [--detail 0..1]`: generate a glTF collider asset.
  `0` (the default) is a bounding box; higher values retain a deterministic
  subset of model triangles, and `1` uses the complete model geometry.
  `--recommend --body static|dynamic|trigger|character` explains an appropriate
  explicit collider without changing the project; `--preview` writes a scene
  using the generated collider.
- `demi asset export --project <project> --output <file.demipack> --asset
  asset://id`: export selected assets and their transitive dependencies as a
  deterministic, checksummed package. Repeat `--asset` to select more roots.
- `demi asset import-package <file.demipack> --project <project>`: verify and
  preview conflicts before importing a portable asset package. Existing paths
  and IDs are never replaced unless `--overwrite` is explicit.
- `demi asset budget <demi.project.json> --platform android|linux`: inspect
  visible instances, unique meshes, procedural triangles, decoded texture
  memory, lights, shadow lights, and transparent draws without starting a
  graphics device. Declared project budgets produce actionable diagnostics.
- `demi cook --project <project> --platform linux [--output path]`: validate
  and produce deterministic runtime-ready project data plus a cook manifest.
- `demi build linux --project <project> [--output path]`: cook the project and
  package it with the Linux runtime and launcher.
- `demi build apk --project <project> [--gradle gradle]`: validate and package
  the same project as an Android debug APK.
- `demi capabilities export [--output path]`: generate the current component
  and Lua binding capability manifest.
- `demi capabilities check [--baseline path] [--format text|json]`: report
  compatible additions and fail on breaking public API changes.
- `demi capabilities verify-gates [--manifest path]`: validate the reference
  games, their declared capability gaps, and their public-only dependencies.
- `demi save inspect <save>`: validate and summarize a save file.
- `demi script check <script>`: parse a Lua script with the embedded Lua 5.4 compiler and report diagnostics.
- `demi lua-stubs generate [path]`: copy the checked-in LuaLS/EmmyLua annotations for the exposed runtime Lua API. The default output is `scripts/stubs/demi.lua`.
- `demi dev [--project <project>]`: find the nearest project, run development
  environment checks, and launch it with last-known-good source watching.
  Runtime flags such as `--profiler`, `--debug-overlays`, and `--max-frames`
  remain available.
- `demi package add <name>@<constraint> --project <project>`: resolve the full
  dependency graph, verify archives, and atomically update the project,
  `demi.packages.lock.json`, and `.demi/packages/`.
- `demi package remove <name> --project <project>`: remove a direct dependency
  and atomically reinstall the remaining resolved graph.
- `demi package install --project <project> [--locked] [--offline] [--dry-run]`:
  restore installed packages. `--locked` performs no version solving and
  `--offline` permits only already verified cache entries.
- `demi package update [name] --project <project>`: update every package, or
  one direct package while preserving all other locked versions.
- `demi package list|outdated --project <project> [--format json]`: inspect the
  installed graph or compatible updates with deterministic machine output.
- `demi package publish [directory] --registry <url-or-path>`: publish one
  immutable package version to an HTTP or directory registry.
- `demi package test [directory] [--format json]`: run declared Lua tests in a
  deterministic isolated package-test world with only declared dependencies.
- `demi test`: run built-in scaffold checks.
- `demi test android --project <project> [--serial device] [--adb adb]
  [--apk apk] [--output directory]`: build, install, and exercise the
  attached physical device with a deterministic qualification scenario.
  It records device/API/ABI/GPU metadata, step timing, screenshots with
  hashes, private files, logcat output, launched PIDs, and fatal markers,
  then writes `qualification.json` under `<project>/build/android/qualification`
  and fails when a required step, runtime marker, or crash check fails.
  Projects that declare `scripts/tests/e2e.lua` run their Lua end-to-end
  tests in-app through the `Test` API instead of adb-driven taps (see
  [E2E Testing](e2e-testing.md)).
- `demi run --project <project> [--max-frames count] [--e2e-tests]`: launch
  the runtime preview. Use `--max-frames 1` for automation. `--e2e-tests`
  runs the project's `scripts/tests/e2e.lua` harness (see
  [E2E Testing](e2e-testing.md)). Alternatively use `demi test linux` for a
  report-producing desktop run.
- `demi run android [--project <project>] [--serial device] [--adb adb]
  [--apk apk] [--watch]`: build a current debug APK, install it on the
  attached device, launch the game, and stream filtered engine/SDL/crash
  logs until stopped. APKs rebuild automatically when engine or project
  sources are newer than the artifact. `--watch` enables the Android
  runtime project watcher: changed project sources are cooked
  incrementally and only changed cooked files are synchronized into the
  debuggable application sandbox for hot reload without reinstalling.
- `demi run linux --project <project> --profiler`: print slow-frame details,
  frame-time percentiles, and a sorted runtime scope report when the run ends.
- `demi run ... --input-replay <fixture.replay.json>`: replay deterministic input frames; the project fixed timestep must match the fixture.
- `demi run ... --profile-report <report.csv>`: write the same aggregate
  runtime scopes to a report file. It also enables collection when
  `--profiler` is omitted.
- `demi run ... --debug-overlays <names>`: override project overlays with a comma-separated list of `colliders`, `contacts`, `grid`, `entity_ids`, `draw_order`, `ui_bounds`, and `profiler`.
- `demi editor [--project <project>]`: launch the native editor. Without an
  explicit path, it discovers the nearest parent `demi.project.json`.

See [Capability Manifest and Reference Gates](capability-gates.md) for the
checked baseline and reference-game workflow.

## Exit Codes

- `0`: success.
- `1`: validation or test failure.
- `2`: CLI usage error.
- `3`: internal engine error.
