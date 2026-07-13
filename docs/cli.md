# CLI Contract

The `demi` CLI is the automation interface for humans, scripts, CI, and AI agents.

## Commands

- `demi --help`: print command help.
- `demi version`: print engine version.
- `demi validate [path]`: validate a project, scene, save, or directory.
- `demi schema export`: list schema files available in `schemas/`.
- `demi scene list <project>`: list scene references from a project file.
- `demi scene inspect <scene>`: validate and summarize a scene file.
- `demi scene diff <old> <new>`: print a deterministic structural scene diff.
- `demi asset inspect <asset>`: validate and summarize an asset manifest.
- `demi asset deps <asset>`: list source-file dependencies for an asset manifest.
- `demi asset import <source> --project <project> --id asset://id`: copy a
  production source into the project, select its importer, generate a cached
  output, and write a complete manifest.
- `demi asset reimport <asset>`: refresh generated output, source hash, and
  importer version after a source change.
- `demi asset export --project <project> --output <file.demipack> --asset
  asset://id`: export selected assets and their transitive dependencies as a
  deterministic, checksummed package. Repeat `--asset` to select more roots.
- `demi asset import-package <file.demipack> --project <project>`: verify and
  preview conflicts before importing a portable asset package. Existing paths
  and IDs are never replaced unless `--overwrite` is explicit.
- `demi cook --project <project> --platform linux [--output path]`: validate
  and produce deterministic runtime-ready project data plus a cook manifest.
- `demi build linux --project <project> [--output path]`: cook the project and
  package it with the Linux runtime and launcher.
- `demi save inspect <save>`: validate and summarize a save file.
- `demi script check <script>`: parse a Lua script with the embedded Lua 5.4 compiler and report diagnostics.
- `demi lua-stubs generate [path]`: copy the checked-in LuaLS/EmmyLua annotations for the exposed runtime Lua API. The default output is `scripts/stubs/demi.lua`.
- `demi test`: run built-in scaffold checks.
- `demi run --project <project> [--frames count]`: launch the runtime preview. Use `--frames 1` for automation.
- `demi run ... --input-replay <fixture.replay.json>`: replay deterministic input frames; the project fixed timestep must match the fixture.
- `demi run ... --profile-report <report.csv>`: aggregate runtime scopes and write a developer profile report.
- `demi run ... --debug-overlays <names>`: override project overlays with a comma-separated list of `colliders`, `contacts`, `grid`, `entity_ids`, `draw_order`, `ui_bounds`, and `profiler`.
- `demi editor --project <project>`: launch the editor target once implemented.

## Exit Codes

- `0`: success.
- `1`: validation or test failure.
- `2`: CLI usage error.
- `3`: internal engine error.
