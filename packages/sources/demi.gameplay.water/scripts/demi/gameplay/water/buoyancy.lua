local Entity = require("demi.entity")
local Transform = require("demi.transform3d")
local Rigidbody = require("demi.physics.rigidbody3d")
local Water = require("demi.terrain.water")
local Motion = require("demi.gameplay.water.motion")

local Buoyancy = {}
Buoyancy.__index = Buoyancy

function Buoyancy.new(entity_id, options)
  options = Motion.options(entity_id, options)
  return setmetatable({
    entity_id = entity_id,
    terrain_id = options.terrain_id,
    half_height = Motion.number(Motion.value(options, "half_height", .5), "half_height", 0, true),
    density_ratio = Motion.number(Motion.value(options, "density_ratio", 1.05), "density_ratio", 0),
    linear_drag = Motion.number(Motion.value(options, "linear_drag", 2), "linear_drag", 0),
    gravity = Motion.vector(Motion.value(options, "gravity", {0, -9.81, 0}), "gravity"),
    state = {active = false, submersion = 0},
  }, Buoyancy)
end

-- Call once from on_fixed_update, before the physics step. This is a virtual
-- upright volume approximation: it adds no collider or angular/righting torque.
function Buoyancy:fixed_update(dt)
  Motion.number(dt, "fixed dt", 0, true)
  local state = self.state
  state.active, state.submersion, state.sample = false, 0, nil
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
  local immersion = Motion.submersion(sample, {x, y, z}, self.half_height)
  state.sample, state.submersion = sample, immersion
  if immersion <= 0 then
    return state
  end
  local force = {}
  for axis = 1, 3 do
    local relative_velocity = body.velocity[axis] - sample.flow_velocity[axis]
    local drag = Motion.drag_delta(relative_velocity, self.linear_drag, immersion, dt) / dt
    local lift = -self.gravity[axis] * self.density_ratio * immersion
    force[axis] = Motion.number(body.mass * (lift + drag), "buoyancy force", -math.huge)
  end
  state.active = Rigidbody.add_force(self.entity_id, force[1], force[2], force[3])
  return state
end

return Buoyancy
