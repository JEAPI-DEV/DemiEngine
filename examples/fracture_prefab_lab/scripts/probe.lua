local Input = require("demi.input")
local Scene = require("demi.scene")
local Destruction3D = require("demi.destruction3d")
local Rigidbody3D = require("demi.rigidbody3d")
local Camera3D = require("demi.camera3d")
local Physics3D = require("demi.physics3d")
local Hud = require("demi.hud")

---@demi_component
local Probe = {}

function Probe:on_update()
  if Input.pressed("reset") then Scene.reload(); return end
  if self.pending then
    local request = self.pending
    local state = Destruction3D.state(request.root)
    if state.status == "failed" then
      Hud.set_text("status", state.error)
      self.pending = nil
    elseif state.revision > request.revision then
      local body = state.parts[request.part]
      if body then
        Rigidbody3D.add_impulse(body, request.direction[1] * 550, request.direction[2] * 550, request.direction[3] * 550)
      end
      Hud.set_text("status", "Applied hit | connected bodies: " .. state.bodies)
      self.pending = nil
    end
  end
  if Input.pressed("strike") and not Input.ui_pointer_captured() then
    local x, y = Input.mouse_position()
    local width, height = Input.viewport_size()
    local ray = Camera3D.screen_ray("camera", x, y, width, height)
    if not ray then return end
    local o, d = ray.origin, ray.direction
    local hit = Physics3D.raycast(o[1], o[2], o[3], d[1], d[2], d[3], 100)
    if not hit or not hit.collider_part_id then return end
    local before = Destruction3D.state(hit.entity_id)
    local ok, error = Destruction3D.damage_part(hit.entity_id, hit.collider_part_id, 1.1)
    if ok then
      self.pending = {root = before.root, part = hit.collider_part_id, revision = before.revision, direction = d}
      Hud.set_text("status", "Hit queued")
    else
      Rigidbody3D.add_impulse(hit.entity_id, d[1] * 550, d[2] * 550, d[3] * 550)
      Hud.set_text("status", error)
    end
  end
end
return Probe
