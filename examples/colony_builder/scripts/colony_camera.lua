local Time = require("demi.time")
local Input = require("demi.input")
local Transform3D = require("demi.transform3d")

---@demi_component
---@description Pans the colony camera along the ground using WASD.
local ColonyCamera = {}

---@demi_property
---@range 1 100
ColonyCamera.pan_speed = 35.0

function ColonyCamera:on_update(dt)
  dt = Time.unscaled_delta_time
  local horizontal = Input.value("move_x")
  local forward = Input.value("move_z")
  local length = math.sqrt(horizontal * horizontal + forward * forward)
  if length == 0 then return end
  if length > 1 then
    horizontal, forward = horizontal / length, forward / length
  end

  -- The camera is an unparented scene root. Project its facing onto the ground
  -- so panning follows the screen without changing the camera's height.
  local fx, _, fz = Transform3D.forward(self.entity_id)
  local ground_length = math.sqrt(fx * fx + fz * fz)
  if ground_length < 0.001 then return end
  fx, fz = fx / ground_length, fz / ground_length
  local distance = self.pan_speed * dt
  Transform3D.add_position(self.entity_id,
    (-fz * horizontal + fx * forward) * distance,
    0,
    (fx * horizontal + fz * forward) * distance)
end

return ColonyCamera
