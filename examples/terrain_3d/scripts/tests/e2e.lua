local Test = require("demi.test")
local Hud = require("demi.hud")
local Transform3D = require("demi.transform3d")
local Route = require("terrain_route")

local function camera_position()
  local x, y, z = Transform3D.get_position("camera")
  Test.expect(x ~= nil, "The tour camera must exist")
  return {x, y, z}
end

local function distance(left, right)
  local x = left[1] - right[1]
  local y = left[2] - right[2]
  local z = left[3] - right[3]
  return math.sqrt(x * x + y * y + z * z)
end

return {tests = {
  {name = "camera tour advances pauses resumes and restarts", func = function()
    Test.expect_scene("scene://terrain_3d/main", 30)
    Test.wait(0.1)
    Test.touch("tour_restart")
    local initial = camera_position()
    Test.wait(0.5)
    Test.expect(distance(initial, camera_position()) > 0.1, "Playing must animate the camera")
    Test.touch("tour_pause")
    Test.expect(Hud.get_text("tour_status") == "PAUSED", "Pause must update the HUD")
    local paused = camera_position()
    Test.wait(0.3)
    Test.expect(distance(paused, camera_position()) < 0.0001, "Paused camera must remain still")
    Test.touch("tour_pause")
    Test.wait(0.3)
    Test.expect(distance(paused, camera_position()) > 0.1, "Resume must advance the camera")
    Test.touch("tour_restart")
    Test.touch("tour_pause")
    Test.expect(Hud.get_text("tour_chapter"):find("01 /", 1, true) == 1, "Restart must return to chapter one")
    Test.expect(distance(camera_position(), Route[1].position) < 6, "Restart must return to the opening view")
  end},
  {name = "chapter controls visit the route and wrap while paused", func = function()
    Test.touch("tour_restart")
    Test.touch("tour_pause")
    for chapter = 2, #Route do
      Test.touch("tour_next")
      local prefix = string.format("%02d /", chapter)
      Test.expect(Hud.get_text("tour_chapter"):find(prefix, 1, true) == 1, "Next must advance the chapter label")
      Test.expect(distance(camera_position(), Route[chapter].position) < 0.001, "Chapter seek must reach its authored pose")
      Test.expect(Hud.get_text("tour_status") == "PAUSED", "Seeking must preserve pause state")
    end
    Test.touch("tour_next")
    Test.expect(Hud.get_text("tour_chapter"):find("01 /", 1, true) == 1, "Final chapter must wrap to the opening")
    Test.expect(distance(camera_position(), Route[1].position) < 0.001, "Loop must return to its authored opening")
    Test.touch("tour_restart")
  end},
}}
