# Mobile End-to-End Testing

Mobile tests are Lua functions that run inside the game on a connected device
(or on desktop) and drive it through the `Mobile` API. Touches resolve HUD
nodes by id and flow through the real input pipeline, so a passing test means
the same path a finger uses works.

## Writing tests

Declare a module at `scripts/tests/mobile.lua` in the project. It returns a
table of tests; each test is a coroutine-driven function:

```lua
-- scripts/tests/mobile.lua
local tests = {}

tests[#tests + 1] = {
  name = "settings save writes through the options screen",
  func = function()
    Mobile.wait(1.5)
    Mobile.touch("menu_button_options")
    Mobile.touch("menu_volume_plus")
    Mobile.touch("menu_back")
  end,
}

tests[#tests + 1] = {
  name = "level select loads the platformer scene",
  func = function()
    Mobile.touch("menu_button_levels")
    Mobile.touch("menu_button_level_1")
    Mobile.expect_scene("scene://minimal_2d_android/platformer", 10.0)
  end,
}

return {tests = tests}
```

The harness runs tests sequentially from the boot scene, logs stable markers
(`[test] PASS <name>.`, `[test] FAIL <name>: <reason>.`,
`[test] SUMMARY passed=N failed=M.`), and quits the app when done. A failed
`Mobile.expect` or a timeout fails the running test and continues with the
next one.

## API

- `Mobile.touch(node_id)`: tap the center of a HUD node. Unknown ids fail the
  test.
- `Mobile.tap(x, y)`: tap a canvas-space position.
- `Mobile.swipe(from_node_id, to_node_id, duration?)`: swipe between two node
  centers (default duration 0.4s).
- `Mobile.swipe_xy(x1, y1, x2, y2, duration?)`: swipe between canvas
  positions.
- `Mobile.wait(seconds)`: wait for game time to pass.
- `Mobile.expect_scene(scene_id, timeout?)`: wait until the active scene
  matches; fails the test on timeout (default 10s).
- `Mobile.expect(condition, message)`: fail the test unless `condition` is
  truthy.
- `Mobile.node_center(node_id)`: resolved canvas-space center, or nil.

## Running

- Device: `demi test android --project <project>`. When
  `scripts/tests/mobile.lua` exists, the tool sets the `.demi_run_tests`
  marker, launches the game, waits for the summary, and records
  `lua_tests` results in `build/android/qualification/qualification.json`.
  The adb-tap lifecycle flow remains available for projects without Lua
  tests.
- Desktop: `demi run --project <project> --mobile-tests`. The same harness
  runs in a window, which makes it usable in local loops and CI.

Tests that need new engine behavior belong behind the same markers as other
runtime features: keep assertions deterministic and prefer node ids over
coordinates so tests survive resolution, orientation, and safe-area changes.
