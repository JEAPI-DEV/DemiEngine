---@demi_component
local Probe = {}

-- @HandleAction("reset")
function Probe:on_reset()
  Scene.reload()
end

-- @HandleAction("impulse")
function Probe:on_impulse()
  Rigidbody3D.add_impulse("arch", 1200, 3000, -400)
  Hud.set_text("status", "Impulse applied to one rigid body; its three visual children follow.")
end

function Probe:on_update()
  if Input.pressed("reset") then
    self:on_reset()
    return
  end
  if Input.pressed("impulse") then
    self:on_impulse()
  end
  if Input.pressed("inspect_part") and not Input.ui_pointer_captured() then
    local x, y = Input.mouse_position()
    local width, height = Input.viewport_size()
    local ray = Camera3D.screen_ray("camera", x, y, width, height)
    local status
    if ray then
      local o, d = ray.origin, ray.direction
      local hit = Physics3D.raycast(o[1], o[2], o[3], d[1], d[2], d[3], 100)
      status = hit and ("Hit " .. hit.entity_id .. " / part " .. (hit.collider_part_id or "none")) or "No collider hit"
    else
      status = "Camera ray unavailable"
    end
    Hud.set_text("status", status)
    Debug.log(status)
  end
end

return Probe
