---@meta
-- Native module: require("demi.test"). Annotations only.
---End-to-end test API. Available when the runtime launches in test mode
---(`demi test linux`, `demi run --e2e-tests`, or the `.demi_run_tests`
---device marker). Touches resolve HUD nodes by id through the live layout
---and flow through the real input pipeline.
---@class TestService
local Test = {}
---Taps the center of a HUD node by id. Yields until the tap completes.
---@param node_id string
function Test.touch(node_id) end
---Taps a canvas-space position.
---@param x number
---@param y number
function Test.tap(x, y) end
---Swipes between two HUD node centers over `duration` seconds.
---@param from_node_id string
---@param to_node_id string
---@param duration? number Defaults to 0.4
function Test.swipe(from_node_id, to_node_id, duration) end
---Swipes between two canvas-space positions over `duration` seconds.
---@param from_x number
---@param from_y number
---@param to_x number
---@param to_y number
---@param duration? number Defaults to 0.4
function Test.swipe_xy(from_x, from_y, to_x, to_y, duration) end
---Waits `seconds` of game time before resuming the test.
---@param seconds number
function Test.wait(seconds) end
---Waits until the active scene id matches, or fails the test on timeout.
---@param scene_id string
---@param timeout? number Defaults to 10
function Test.expect_scene(scene_id, timeout) end
---Fails the running test with `message` unless `condition` is truthy.
---@param condition any
---@param message string
function Test.expect(condition, message) end
---Returns the resolved center of a HUD node in canvas units, or nil.
---@param node_id string
---@return number[]?
function Test.node_center(node_id) end

return Test
