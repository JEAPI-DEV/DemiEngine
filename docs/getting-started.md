# Getting Started

Create a project from one of the checked-in starter templates:

```sh
./build/linux-debug/demi new --list
./build/linux-debug/demi new games/my_game --template platformer --name "My Game"
```

`demi new` never overwrites a destination. It writes into a temporary sibling,
validates the complete project, and renames that directory into place only when
validation succeeds. Use `--dry-run` to inspect the file list without writing.

The generated project contains source JSON, a Lua entry script, a deterministic
smoke replay, and a project-local copy of the generated LuaLS engine stubs.

Before running or packaging, inspect the local environment:

```sh
./build/linux-debug/demi doctor --project games/my_game/demi.project.json
./build/linux-debug/demi doctor --project games/my_game/demi.project.json \
  --platform android --format json
```

Doctor diagnostics have stable codes and use the same text or JSON diagnostic
format as validation. Android checks include Java, SDK, and NDK discovery.

Enter the project and start the complete development loop:

```sh
cd games/my_game
../../build/linux-debug/demi dev
```

`demi dev` finds the nearest `demi.project.json`, runs the relevant environment
and project checks, and starts the runtime with source watching enabled. Pass
`--project` when invoking it from outside the project tree.

Watch mode observes source files at frame boundaries. Lua tables are prepared
before replacing their live version. Scene and HUD edits use scene preparation,
and renderer assets are loaded into a candidate renderer before the live
renderer is swapped. Invalid changes print diagnostics and keep the last good
world and renderer active. Build output, saves, `.git`, and generated files are
excluded from watching.

Lua modules can declare typed, defaulted configuration through
[`property_schema`](script-properties.md). Invalid property overrides reject
activation and leave the last valid watched script running.

For a quick source validation and one-frame headless smoke test:

```sh
DEMI_HEADLESS=1 ./build/linux-debug/demi test \
  --project games/my_game/demi.project.json
```
