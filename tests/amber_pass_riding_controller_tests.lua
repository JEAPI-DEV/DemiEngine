-- Run from the repository root: lua tests/amber_pass_riding_controller_tests.lua
-- These tests execute the example controller with narrow native-service doubles.
-- Native collision and actual HUD input remain covered by its project E2E test.
local controller_path = "examples/amber_pass_3d/scripts/riding_controller.lua"

local function expect(condition, message)
  assert(condition, message)
end

local function fixture(yaw)
  local positions = {
    mount = { 0.0, 0.8, 0.0 },
    camera = { 0.0, 3.6, -7.3 },
  }
  local rotations = { mount = { 0.0, yaw or 0.0, 0.0 } }
  local input = { steering = 0.0, forward = 0.0, gallop = false }
  local motion = { x = 0.0, z = 0.0 }
  local camera_target

  package.loaded["demi.application"] = {}
  package.loaded["demi.events"] = {}
  package.loaded["demi.hud"] = {}
  package.loaded["demi.animation"] = {
    set_root_motion = function() end,
    play = function() return true end,
  }
  package.loaded["demi.input"] = {
    vector = function() return input.steering, input.forward end,
    down = function() return input.gallop end,
    pressed = function() return false end,
    mouse_down = function() return false end,
    ui_pointer_captured = function() return false end,
  }
  package.loaded["demi.physics.query3d"] = {
    sphere_cast = function() return nil end,
  }
  package.loaded["demi.physics.character_controller3d"] = {
    set_velocity = function(_, x, _, z)
      motion.x = x
      motion.z = z
      return true
    end,
  }
  package.loaded["demi.transform3d"] = {
    get_position = function(id) return table.unpack(positions[id]) end,
    get_world_position = function(id) return table.unpack(positions[id]) end,
    get_rotation = function(id) return table.unpack(rotations[id]) end,
    set_position = function(id, x, y, z) positions[id] = { x, y, z } end,
    set_rotation = function(id, x, y, z) rotations[id] = { x, y, z } end,
    look_at = function(_, x, y, z) camera_target = { x, y, z } end,
  }

  local Controller = dofile(controller_path)
  local rider = setmetatable({ entity_id = "mount" }, { __index = Controller })
  rider:on_create()
  local result = { rider = rider, input = input, positions = positions, motion = motion }

  function result:step(dt)
    rider:on_update(dt)
    rider:on_fixed_update(dt)
    positions.mount[1] = positions.mount[1] + motion.x * dt
    positions.mount[3] = positions.mount[3] + motion.z * dt
  end

  function result:screen_right()
    rider:update_camera(0.0)
    local camera = positions.camera
    local forward_x = camera_target[1] - camera[1]
    local forward_z = camera_target[3] - camera[3]
    -- Camera3DMath uses cross(forward, up), with authored up = +Y.
    local right_x = -forward_z
    local right_z = forward_x
    local length = math.sqrt(right_x * right_x + right_z * right_z)
    return right_x / length, right_z / length
  end

  return result
end

local function steering_tracks_camera_right()
  for _, yaw in ipairs({ 0.0, math.pi * 0.5, -math.pi * 0.75 }) do
    for _, steering in ipairs({ -1.0, 1.0 }) do
      for _, travel in ipairs({ -1.0, 1.0 }) do
        local probe = fixture(yaw)
        local right_x, right_z = probe:screen_right()
        probe.rider.speed = travel * 4.0
        probe.input.forward = -travel
        probe.input.steering = steering
        probe:step(1.0 / 60.0)
        local screen_motion = probe.motion.x * right_x + probe.motion.z * right_z
        expect(screen_motion * steering > 0.0,
          "A/D must move toward the matching side of the chase camera, including reverse")
      end
    end
  end
end

local function released_input_brakes_walk_and_gallop()
  for _, dt in ipairs({ 1.0 / 120.0, 1.0 / 60.0, 1.0 / 30.0 }) do
    for _, galloping in ipairs({ false, true }) do
      local probe = fixture()
      probe.input.forward = -1.0
      probe.input.gallop = galloping
      local maximum_speed = galloping and probe.rider.gallop_speed or probe.rider.walk_speed
      for frame = 1, math.ceil(2.0 / dt) do
        probe:step(dt)
      end
      expect(math.abs(probe.rider.speed - maximum_speed) < 0.000001,
        "Held input must reach the authored walk or gallop speed")

      probe.input.forward = 0.0
      local release_z = probe.positions.mount[3]
      local elapsed = 0.0
      while probe.rider.speed ~= 0.0 and elapsed < 0.5 do
        probe:step(dt)
        elapsed = elapsed + dt
      end
      expect(elapsed <= 0.25, "Releasing walk or gallop must stop within a quarter second")
      local stopping_distance = probe.positions.mount[3] - release_z
      local allowed_distance = galloping and 1.1 or 0.25
      expect(stopping_distance <= allowed_distance, "Release must brake rather than coast")

      local stopped_x = probe.positions.mount[1]
      local stopped_z = probe.positions.mount[3]
      for frame = 1, 60 do
        probe:step(dt)
      end
      expect(probe.positions.mount[1] == stopped_x and probe.positions.mount[3] == stopped_z,
        "A stopped mount must have no residual horizontal drift")
    end
  end
end

local function opposite_throttle_brakes_before_reversing()
  local probe = fixture()
  probe.rider.speed = probe.rider.gallop_speed
  probe.input.forward = 1.0
  local elapsed = 0.0
  while probe.rider.speed > 0.0 and elapsed < 0.5 do
    probe:step(1.0 / 60.0)
    elapsed = elapsed + 1.0 / 60.0
  end
  expect(probe.rider.speed == 0.0 and elapsed <= 0.25,
    "Opposite throttle must brake through a stopped state before reverse acceleration")
  probe:step(1.0 / 60.0)
  expect(probe.rider.speed < 0.0, "Held reverse must accelerate after braking finishes")
end

steering_tracks_camera_right()
released_input_brakes_walk_and_gallop()
opposite_throttle_brakes_before_reversing()
print("PASS Amber Pass riding: camera-relative steering, walk/gallop braking and reversal")
