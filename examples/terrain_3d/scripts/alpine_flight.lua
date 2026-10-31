local Transform3D = require("demi.transform3d")
local Input = require("demi.input")
local Application = require("demi.application")
local Hud = require("demi.hud")

---@demi_component
---@display_name Regional terrain flight
local Flight = {}

---@demi_property
Flight.speed = 800
---@demi_property
Flight.turn_speed = 0.7
---@demi_property
Flight.initial_heading = 1.16
---@demi_property
Flight.look_slope = 0.02
---@demi_property
Flight.mouse_sensitivity = 0.003

function Flight:on_start()
  self.heading = self.initial_heading
  self.pitch = math.atan(self.look_slope)
  self.start_position = {Transform3D.get_position("camera")}
end

function Flight:on_update(dt)
  if Input.key_pressed("escape") then
    Application.quit()
    return
  end
  if Input.key_pressed("r") then
    Transform3D.set_position("camera", table.unpack(self.start_position))
    self.heading = self.initial_heading
    self.pitch = math.atan(self.look_slope)
  end
  local turn = (Input.key_down("right") and 1 or 0) - (Input.key_down("left") and 1 or 0)
  self.heading = self.heading - turn * self.turn_speed * dt
  local looking = Input.mouse_down("right") and not Input.ui_pointer_captured()
  if looking ~= self.looking then
    self.looking = looking
    Application.set_mouse_captured(looking)
  elseif looking then
    local dx, dy = Input.mouse_delta()
    self.heading = self.heading - dx * self.mouse_sensitivity
    self.pitch = math.max(-1.45, math.min(1.45, self.pitch - dy * self.mouse_sensitivity))
  end
  local pitch_input = (Input.key_down("up") and 1 or 0) - (Input.key_down("down") and 1 or 0)
  self.pitch = math.max(-1.45, math.min(1.45, self.pitch + pitch_input * self.turn_speed * dt))
  local forward = (Input.key_down("w") and 1 or 0) - (Input.key_down("s") and 1 or 0)
  local side = (Input.key_down("d") and 1 or 0) - (Input.key_down("a") and 1 or 0)
  local up = (Input.key_down("e") and 1 or 0) - (Input.key_down("q") and 1 or 0)
  local distance = self.speed * dt * (Input.key_down("left shift") and 8 or 1)
  local sine, cosine = math.sin(self.heading), math.cos(self.heading)
  local horizontal = math.cos(self.pitch)
  local dx = forward * sine * horizontal - side * cosine
  local dy = forward * math.sin(self.pitch) + up
  local dz = forward * cosine * horizontal + side * sine
  local magnitude = math.max(1, math.sqrt(dx * dx + dy * dy + dz * dz))
  Transform3D.add_position("camera", dx / magnitude * distance,
      dy / magnitude * distance, dz / magnitude * distance)
  local x, y, z = Transform3D.get_position("camera")
  Transform3D.look_at("camera", x + sine, y + math.tan(self.pitch), z + cosine)
  self.hud_elapsed = (self.hud_elapsed or 0) + dt
  if self.hud_elapsed >= 0.25 then
    Hud.set_text("flight_position", string.format("X %.2f km  ·  Z %.2f km  ·  Altitude %.0f m", x / 1000, z / 1000, y))
    self.hud_elapsed = 0
  end
end

function Flight:on_destroy()
  Application.set_mouse_captured(false)
end

return Flight
