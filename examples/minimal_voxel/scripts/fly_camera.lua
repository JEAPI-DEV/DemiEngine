local FlyCamera = {}

local pause_hud_ids = {
  "pause_dim",
  "pause_panel",
  "pause_title",
  "pause_body",
  "pause_resume",
}

local function clamp(value, min_value, max_value)
  if value < min_value then
    return min_value
  end
  if value > max_value then
    return max_value
  end
  return value
end

local function set_pause_hud_visible(visible)
  for _, id in ipairs(pause_hud_ids) do
    Hud.set_visible(id, visible)
  end
end

function FlyCamera:set_paused(paused)
  self.paused = paused
  Runtime.set_mouse_captured(not paused)
  set_pause_hud_visible(paused)
end

function FlyCamera:on_create()
  self.speed = 10.0
  self.fast_speed = 24.0
  self.sensitivity = 0.003
  self.yaw = 0.0
  self.pitch = -0.35
  self.paused = false
  Hud.text("hud_hint", "WASD fly - Space/Shift vertical - Mouse look - ESC pause", 20.0, 20.0, 2.5)
  Hud.text("hud_pos", "pos: (0.0, 0.0, 0.0)", 20.0, 56.0, 2.5)
  Hud.rect("pause_dim", 0.0, 0.0, 960.0, 540.0, 0.02, 0.02, 0.03, 0.68)
  Hud.rect("pause_panel", 300.0, 176.0, 360.0, 188.0, 0.08, 0.09, 0.11, 0.94)
  Hud.text("pause_title", "PAUSED", 378.0, 214.0, 6.0, 0.94, 0.98, 1.0, 1.0)
  Hud.text("pause_body", "MOUSE IS FREE", 360.0, 284.0, 3.0, 0.78, 0.86, 0.92, 1.0)
  Hud.text("pause_resume", "ESC TO RESUME", 352.0, 322.0, 3.0, 1.0, 0.82, 0.44, 1.0)
  set_pause_hud_visible(false)
end

function FlyCamera:on_start()
  Runtime.set_mouse_captured(true)
end

function FlyCamera:on_update(dt)
  if Input.is_pressed("escape") then
    self:set_paused(not self.paused)
  end

  if self.paused then
    return
  end

  local dx, dy = Input.mouse_delta()
  self.yaw = self.yaw - dx * self.sensitivity
  self.pitch = clamp(self.pitch - dy * self.sensitivity, -1.45, 1.45)
  Transform3D.set_rotation(self.entity_id, self.pitch, self.yaw, 0.0)

  local right = Input.axis("a", "d")
  local forward = Input.axis("s", "w")
  local vertical = Input.axis("left shift", "space")
  local speed = Input.is_down("left ctrl") and self.fast_speed or self.speed

  local sin_y = math.sin(self.yaw)
  local cos_y = math.cos(self.yaw)
  local move_x = (right * cos_y) - (forward * sin_y)
  local move_z = (-right * sin_y) - (forward * cos_y)

  if right ~= 0.0 or forward ~= 0.0 or vertical ~= 0.0 then
    Transform3D.add_position(self.entity_id, move_x * speed * dt, vertical * speed * dt, move_z * speed * dt)
  end

  local x, y, z = Transform3D.get_position(self.entity_id)
  Hud.set_text("hud_pos", string.format("pos: (%.1f, %.1f, %.1f)", x or 0.0, y or 0.0, z or 0.0), 20.0, 56.0, 2.5)
end

function FlyCamera:on_destroy()
  Runtime.set_mouse_captured(false)
end

return FlyCamera
