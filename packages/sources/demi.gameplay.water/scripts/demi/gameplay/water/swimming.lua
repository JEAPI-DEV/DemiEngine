local Entity = require("demi.entity")
local Transform = require("demi.transform3d")
local Rigidbody = require("demi.physics.rigidbody3d")
local Water = require("demi.terrain.water")
local Motion = require("demi.gameplay.water.motion")

local Swimming = {}
Swimming.__index = Swimming

function Swimming.new(entity_id, options)
  options = Motion.options(entity_id, options)
  return setmetatable({
    entity_id = entity_id,
    terrain_id = options.terrain_id,
    speed = Motion.number(Motion.value(options, "speed", 3), "speed", 0),
    acceleration = Motion.number(Motion.value(options, "acceleration", 8), "acceleration", 0),
    linear_drag = Motion.number(Motion.value(options, "linear_drag", 2), "linear_drag", 0),
    min_depth = Motion.number(Motion.value(options, "min_depth", 1.5), "min_depth", 0),
    max_flow = Motion.number(Motion.value(options, "max_flow", 2.5), "max_flow", 0),
    state = {active = false},
  }, Swimming)
end

-- Input is a world-space direction, including optional ascent/descent. The
-- caller owns camera-relative input, animation, oxygen and land-controller mode.
function Swimming:fixed_update(dt, direction)
  Motion.number(dt, "fixed dt", 0, true)
  if direction == nil then
    direction = {0, 0, 0}
  end
  direction = Motion.clamp_direction(Motion.vector(direction, "swim direction"))
  local state = self.state
  state.active, state.sample = false, nil
  local body = Rigidbody.state(self.entity_id)
  if not body or body.body_type ~= "dynamic" or not body.enabled
    or not Entity.is_enabled(self.entity_id) then
    return state
  end
  local x, y, z = Transform.get_world_position(self.entity_id)
  if x == nil then
    return state
  end
  local sample = Water.sample({x, y, z}, self.terrain_id)
  state.sample = sample
  if not sample or not sample.underwater or sample.depth < self.min_depth then
    return state
  end
  local flow = sample.flow_velocity
  local flow_speed = math.sqrt(flow[1]^2 + flow[2]^2 + flow[3]^2)
  if flow_speed > self.max_flow then
    return state
  end
  local requested = {}
  local length_squared = 0
  for axis = 1, 3 do
    local relative_velocity = body.velocity[axis] - sample.flow_velocity[axis]
    local desired = direction[axis] * self.speed
    requested[axis] = desired - relative_velocity
    length_squared = length_squared + requested[axis]^2
  end
  local length = math.sqrt(length_squared)
  local amount = length > 0 and math.min(1, self.acceleration * dt / length) or 0
  local force = {}
  for axis = 1, 3 do
    local relative_velocity = body.velocity[axis] - sample.flow_velocity[axis]
    local driven = relative_velocity + requested[axis] * amount
    local damping = Motion.drag_delta(driven, self.linear_drag, 1, dt)
    force[axis] = Motion.number(body.mass * (requested[axis] * amount + damping) / dt,
      "swimming force", -math.huge)
  end
  state.active = Rigidbody.add_force(self.entity_id, force[1], force[2], force[3])
  return state
end

return Swimming
