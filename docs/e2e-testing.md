# End-to-End Testing

E2E tests are Lua functions that run inside the game on a connected device
(or on desktop) and drive it through the `Test` API. Touches resolve HUD
nodes by id and flow through the real input pipeline, so a passing test means
the same path a finger uses works.

## Writing tests

Declare a module at `scripts/tests/e2e.lua` in the project. It returns a
table of tests; each test is a coroutine-driven function:

```lua
-- scripts/tests/e2e.lua
local tests = {}

tests[#tests + 1] = {
  name = "settings save writes through the options screen",
  func = function()
    Test.wait(1.5)
    Test.touch("menu_button_options")
    Test.touch("menu_volume_plus")
    Test.touch("menu_back")
  end,
}

tests[#tests + 1] = {
  name = "level select loads the platformer scene",
  func = function()
    Test.touch("menu_button_levels")
    Test.touch("menu_button_level_1")
    Test.expect_scene("scene://minimal_2d_android/platformer", 10.0)
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

- `Test.touch(node_id)`: tap the center of a HUD node. Unknown ids fail the
  test.
- `Test.tap(x, y)`: tap a canvas-space position.
- `Test.swipe(from_node_id, to_node_id, duration?)`: swipe between two node
  centers (default duration 0.4s).
- `Test.swipe_xy(x1, y1, x2, y2, duration?)`: swipe between canvas
  positions.
- `Test.wait(seconds)`: wait for game time to pass.
- `Test.expect_scene(scene_id, timeout?)`: wait until the active scene
  matches; fails the test on timeout (default 10s).
- `Test.expect(condition, message)`: fail the test unless `condition` is
  truthy.
- `Test.node_center(node_id)`: resolved canvas-space center, or nil.

TLS endpoints are also scriptable for loopback tests: `TlsServer.listen`,
`TlsServer.send`, `TlsServer.client_connected(id)`, `TlsClient.connect`,
`TlsClient.send`, `TlsClient.is_connected`, and `TlsClient.events`. Note that
the game code and the test harness share one TLS client event stream: game
modules that call `TlsClient.events()` drain the same queue, so prefer the
polled state (`TlsClient.is_connected`, `TlsServer.client_connected`) over
the `connected` event inside tests, and pump `TlsServer.events()` to flush
queued sends.

## Running

- Device: `demi test android --project <project>`. When
  `scripts/tests/e2e.lua` exists, the tool sets the `.demi_run_tests`
  marker, launches the game, waits for the summary, and records
  `lua_tests` results in `build/android/qualification/qualification.json`.
  The adb-tap lifecycle flow remains available for projects without Lua
  tests.
- Desktop: `demi test linux --project <project>`. The same harness
  runs in a window, which makes it usable in local loops and CI.

Tests that need new engine behavior belong behind the same markers as other
runtime features: keep assertions deterministic and prefer node ids over
coordinates so tests survive resolution, orientation, and safe-area changes.
