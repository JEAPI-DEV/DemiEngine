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
and project checks, and starts the runtime with watch mode enabled. Pass
`--project` when invoking it from outside the project tree.

Watch mode means you can edit Lua, scene, HUD, data, and asset files while the
game keeps running: changes hot-reload into the running world without a
restart. Changes are prepared at frame boundaries before they replace the live
version, and invalid changes print diagnostics while the last good world and
renderer stay active. Build output, saves, `.git`, and generated files are
excluded from watching. On an attached Android device the same flow works
through `demi run android --watch`; see the
[CLI reference](cli.md) and the
[Android workflow](android-lifecycle.md#attached-device-workflow).

Lua modules can declare typed, defaulted configuration through
[`property_schema`](script-properties.md). Invalid property overrides reject
activation and leave the last valid watched script running.

For a quick source validation and one-frame headless smoke test:

```sh
DEMI_HEADLESS=1 ./build/linux-debug/demi test \
  --project games/my_game/demi.project.json
```
