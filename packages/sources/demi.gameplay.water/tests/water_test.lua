local Test = require("demi.test")
local Motion = require("demi.gameplay.water.motion")

Test.case("volume clips to water and bed along the transformed column", function()
  local sample = {surface = {10, 0, 0}, column_up = {1, 0, 0}, depth = 2}
  Test.equal(Motion.submersion(sample, {10, 0, 0}, 1), .5)
  Test.equal(Motion.submersion(sample, {9, 0, 0}, 1), 1)
  Test.equal(Motion.submersion(sample, {8, 0, 0}, 1), .5)
  Test.equal(Motion.submersion(sample, {7, 0, 0}, 1), 0)
  Test.equal(Motion.submersion(sample, {11, 0, 0}, 1), 0)
  Test.equal(Motion.submersion(nil, {9, 0, 0}, 1), 0)
end)

Test.case("exponential water drag composes across fixed-step subdivisions", function()
  local full = 8 + Motion.drag_delta(8, 2, .75, 1)
  local subdivided = 8
  for _ = 1, 20 do
    subdivided = subdivided + Motion.drag_delta(subdivided, 2, .75, .05)
  end
  Test.truthy(math.abs(full - subdivided) < 1e-12)
  Test.truthy(full > 0 and full < 8)
  Test.equal(Motion.drag_delta(8, 2, 0, 1), 0)
end)

Test.case("directions are copied and length limited without diagonal advantage", function()
  local supplied = {3, 4, 0}
  local result = Motion.clamp_direction(Motion.vector(supplied, "direction"))
  Test.equal(result[1], .6)
  Test.equal(result[2], .8)
  Test.equal(supplied[1], 3)
  local huge = Motion.clamp_direction({1e300, 1e300, 0})
  Test.truthy(math.abs(huge[1] - math.sqrt(.5)) < 1e-12)
end)

Test.case("invalid finite configuration is rejected", function()
  for _, value in ipairs({-1, math.huge, 0/0, false}) do
    Test.equal(pcall(function() Motion.number(value, "rate", 0) end), false)
  end
  Test.equal(pcall(function() Motion.vector({1, 2}, "gravity") end), false)
  Test.equal(pcall(function() Motion.vector({1, 2, 3, 4}, "gravity") end), false)
  Test.equal(pcall(function() Motion.options("", {}) end), false)
end)
