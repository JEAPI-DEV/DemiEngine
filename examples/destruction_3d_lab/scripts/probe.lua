local Debug = require("demi.debug")
local Input = require("demi.input")
local Scene = require("demi.scene")
local Destruction3D = require("demi.physics.destruction3d")
local Rigidbody3D = require("demi.physics.rigidbody3d")
local Camera3D = require("demi.camera3d")
local Physics3D = require("demi.physics.query3d")
local Hud = require("demi.hud")

---@demi_component
local Probe = {}

-- @HandleAction("reset")
function Probe:on_reset()
  Scene.reload()
end

-- @HandleAction("impulse")
function Probe:on_impulse()
  self:hit("arch", "lintel", 1)
end

function Probe:hit(entity, part, damage)
  local before = Destruction3D.state(entity)
  local ok, issue = Destruction3D.damage_part(entity, part, damage)
  if ok then
    self.pending_hit = {root = before.root, part = part, revision = before.revision}
    Hud.set_text("status", "Damage queued: " .. part)
  else
    Hud.set_text("status", issue)
    Debug.log(issue)
  end
end

function Probe:on_update()
  if self.pending_hit then
    local hit = self.pending_hit
    local state = Destruction3D.state(hit.root)
    if state.status == "failed" then
      Hud.set_text("status", state.error)
      Debug.log(state.error)
      self.pending_hit = nil
    elseif state.revision > hit.revision then
      -- Strike impulse is gameplay; splitting, reparenting and motion inheritance
      -- belong to the engine. Anchored/static parts ignore this impulse.
      local body = state.parts[hit.part]
      if body then Rigidbody3D.add_impulse(body, 800, 0, -2400) end
      local status = "Damage applied | physical bodies " .. state.bodies
      Hud.set_text("status", status)
      Debug.log(status)
      self.pending_hit = nil
    end
  end
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
      if hit and hit.collider_part_id then
        Debug.log(status)
        self:hit(hit.entity_id, hit.collider_part_id, 0.6)
        return
      end
    else
      status = "Camera ray unavailable"
    end
    Hud.set_text("status", status)
    Debug.log(status)
  end
end

return Probe
