local Input = require("demi.input")
local Scene = require("demi.scene")
local Destruction3D = require("demi.physics.destruction3d")
local Vector3 = require("demi.math.vector3")
local Camera3D = require("demi.camera3d")
local Physics3D = require("demi.physics.query3d")
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
      Hud.set_text("status", "Applied spatial impact | connected bodies: " .. state.bodies)
      self.pending = nil
    end
  end
  local blast = Input.pressed("blast")
  if (Input.pressed("strike") or blast) and not Input.ui_pointer_captured() then
    local x, y = Input.mouse_position()
    local width, height = Input.viewport_size()
    local ray = Camera3D.screen_ray("camera", x, y, width, height)
    if not ray then return end
    local o, d = ray.origin, ray.direction
    local hit = Physics3D.raycast(o[1], o[2], o[3], d[1], d[2], d[3], 100)
    if not hit then return end
    local before = Destruction3D.state(hit.entity_id)
    if before.status == "unattached" then return end
    local ok, error, affected = Destruction3D.impact({
      position = blast and Vector3.add(hit.point, Vector3.scale(hit.normal, 0.1)) or hit.point,
      radius = blast and 1.2 or 0.3,
      energy = blast and 24000 or 6000,
      impulse = blast and 1200 or 180,
      direction = not blast and d or nil,
      entity = not blast and hit.entity_id or nil,
    })
    if ok then
      self.pending = {root = before.root, revision = before.revision}
      Hud.set_text("status", "Spatial impact queued | affected assemblies: " .. affected)
    else
      Hud.set_text("status", error)
    end
  end
end
return Probe
