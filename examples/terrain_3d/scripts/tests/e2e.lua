local Test = require("demi.test")
local Hud = require("demi.hud")
local Transform3D = require("demi.transform3d")
local Route = require("terrain_route")
local Scene = require("demi.scene")
local MountainRoute = require("mountain_route")
local Physics3D = require("demi.physics.query3d")

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
  {name = "regional terrain covers distant teleports and revisits", func = function()
    Test.expect_scene("scene://terrain_3d/alpine", 30)
    -- Drive the real camera through LOD boundaries without desktop input.
    -- Wait for initial collision coverage before measuring moving-camera work.
    local opening
    for attempt = 1, 300 do
      Test.wait(0.1)
      opening = Physics3D.raycast(0, 6000, 0, 0, -1, 0, 12000)
      if opening then break end
    end
    Test.expect(opening ~= nil, "Opening regional cover must finish preparing")
    local altitude = math.max(400, opening.point[2] + 120)
    for step = 0, 40 do
      Transform3D.set_position("camera", step * 50, altitude, step * 20)
      Test.wait(0.1)
    end
    Test.wait(1)
    for step = 40, 0, -1 do
      Transform3D.set_position("camera", step * 50, altitude, step * 20)
      Test.wait(0.1)
    end
    Test.expect(Physics3D.raycast(0, 6000, 0, 0, -1, 0, 12000) ~= nil,
                "Morphing traversal must retain collision coverage on return")
    local points = {{0, 0}, {-20000, -10000}, {50000, 50000},
                    {-100000, -90000}, {100000, 90000}, {0, 0}}
    for _, point in ipairs(points) do
      Transform3D.set_position("camera", point[1], 4500, point[2])
      local hit
      for attempt = 1, 100 do
        Test.wait(0.1)
        hit = Physics3D.raycast(point[1], 6000, point[2], 0, -1, 0, 12000)
        if hit then break end
      end
      Test.expect(hit ~= nil, "Streamed terrain must retain collision coverage at distant viewpoints")
      Test.wait(2)
    end
    Transform3D.set_position("camera", 0, 400, 0)
    Test.wait(5)
    Test.expect(Physics3D.raycast(0, 6000, 0, 0, -1, 0, 12000) ~= nil,
                "Returning to the opening valley must restore terrain coverage")
    Test.expect(Scene.load("scene://terrain_3d/loading"), "Unload the regional world")
    Test.expect_scene("scene://terrain_3d/main", 90)
  end},
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
  {name = "large mountain scene prepares traverses and unloads", func = function()
    Test.expect(Scene.load("scene://terrain_3d/mountain_loading"), "Open highland loading scene")
    Test.expect_scene("scene://terrain_3d/mountain_range", 120)
    Test.wait(2)
    Test.expect(Hud.get_text("tour_title") == "SILVERMERE HIGHLANDS", "Highlands use their own tour")
    Test.touch("tour_restart")
    Test.touch("tour_pause")
    for chapter = 2, #MountainRoute do
      Test.touch("tour_next")
      Test.wait(0.25)
      local position = camera_position()
      local expected = MountainRoute[chapter].position
      Test.expect(math.abs(position[1] - expected[1]) < 0.01 and math.abs(position[3] - expected[3]) < 0.01,
                  string.format("Highland chapter %d expected %.1f, %.1f; got %.3f, %.3f (%s)", chapter, expected[1], expected[3], position[1], position[3], Hud.get_text("tour_status")))
      Test.expect(position[2] >= expected[2], "Ground clearance may raise but must not lower the camera")
    end
    Test.expect(Scene.load("scene://terrain_3d/loading"), "Unload highlands and return")
    Test.expect_scene("scene://terrain_3d/main", 90)
    Test.wait(0.3)
    Test.expect(Hud.get_text("tour_title") == "SILVERMERE", "Return restores the original scene")
  end},
}}
