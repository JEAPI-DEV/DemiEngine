local Test = require("demi.test")
local Path = require("demi.camera.path")

local frames = {
  {position = {0, 10, 0}, target = {0, 0, 0}},
  {position = {10, 10, 0}, target = {10, 0, 0}},
  {position = {10, 10, 10}, target = {10, 0, 10}},
  {position = {0, 10, 10}, target = {0, 0, 10}},
}

Test.case("closed path interpolates position and target and wraps time", function()
  local path = Path.new(frames, {duration = 8})
  local midpoint = path:sample(1)
  Test.near(midpoint.position[1], 5)
  Test.near(midpoint.position[3], -1.25)
  Test.near(midpoint.target[1], 5)
  Test.near(midpoint.target[2], 0)
  Test.equal(path:sample(2).segment, 2)
  Test.near(path:sample(8).position[1], 0)
  Test.near(path:sample(-1).progress, 0.875)
  Test.near(path:update(25).elapsed, 1)
  -- Sampling must not advance playback or expose mutable internal vectors.
  midpoint.position[1] = 999
  Test.near(path:sample(1).position[1], 5)
  Test.near(path.elapsed, 1)
end)

Test.case("pause seek resume and restart preserve explicit playback state", function()
  local path = Path.new(frames, {duration = 8})
  path:pause()
  Test.near(path:update(3).elapsed, 0)
  Test.near(path:seek(4).progress, 0.5)
  Test.equal(path.playing, false)
  Test.near(path:update(2).elapsed, 4)
  path:play()
  Test.near(path:update(1).elapsed, 5)
  Test.near(path:restart().elapsed, 0)
  Test.equal(path.playing, true)
end)

Test.case("nonlooping playback reaches and retains the authored final pose", function()
  local path = Path.new(frames, {duration = 6, loop = false})
  local final = path:update(20)
  Test.near(final.position[1], 0)
  Test.near(final.position[3], 10)
  Test.near(final.progress, 1)
  Test.equal(path.playing, false)
  Test.near(path:update(20).elapsed, 6)
  Test.near(path:seek(-20).elapsed, 0)
end)

Test.case("paths own authored coordinates and reject unsafe inputs", function()
  local authored = {
    {position = {0, 10, 0}, target = {0, 0, 0}},
    {position = {10, 10, 0}, target = {10, 0, 0}},
  }
  local path = Path.new(authored, {duration = 2})
  authored[1].position[1] = 900
  Test.near(path:sample(0).position[1], 0)
  Test.equal(pcall(Path.new, {}, {duration = 2}), false)
  Test.equal(pcall(Path.new, frames, {duration = 0}), false)
  Test.equal(pcall(Path.new, frames, {duration = math.huge}), false)
  Test.equal(pcall(function() path:update(-1) end), false)
  Test.equal(pcall(function() path:seek(0 / 0) end), false)
end)
