local Input = require("demi.input")
local MeshDeformation = require("demi.mesh.deformation")
local Entity = require("demi.entity")
local Scene = require("demi.scene")
local Camera3D = require("demi.camera3d")
local Physics3D = require("demi.physics.query3d")
local Hud = require("demi.hud")

local Demo = {}

function Demo:on_start()
  self.mass, self.speed, self.shots = 2, 8, 0
  self:update_controls()
end

function Demo:update_controls()
  Hud.set_text("dent_mass", "MASS " .. self.mass .. " kg (M)")
  Hud.set_text("dent_speed", "SPEED " .. self.speed .. " m/s (V)")
end

function Demo:fire(origin, direction)
  if self.projectile then Entity.destroy(self.projectile) end
  self.shots = self.shots + 1
  self.projectile = "dent_projectile_" .. self.shots
  self.age, self.impacted = 0, false
  Entity.create(self.projectile, { components = {
    Transform3D = { position = origin },
    MeshRenderer = { shape = "sphere", size = {0.16, 0.16, 0.16}, color = {0.7, 0.8, 0.95, 1} },
    SphereCollider3D = { radius = 0.08 },
    Rigidbody3D = { mass = self.mass, velocity = {
      direction[1] * self.speed, direction[2] * self.speed, direction[3] * self.speed,
    }, use_gravity = false, continuous = true, linear_damping = 0, restitution = 0.1 },
  } })
end

-- @OnEvent("physics3d_collision_enter")
function Demo:on_impact(contact)
  if contact.entity_id ~= "dent_barrel" or contact.other_entity_id ~= self.projectile or self.impacted then return end
  self.impacted = true
  -- All energy and deformation calculations live in the engine.
  local ok, err, depth = MeshDeformation.impact("dent_barrel", contact)
  Hud.set_text("dent_status", ok and string.format("Impact %.1f J | Dent %.1f mm | Collision hull unchanged",
    contact.impact_energy, depth * 1000) or err)
end

-- @HandleAction("dent_hit")
function Demo:on_dent_hit()
  self:fire({0.7, 0.475, 2}, {0, 0, -1})
end

-- @HandleAction("dent_reset")
function Demo:on_dent_reset()
  if self.projectile then Entity.destroy(self.projectile); self.projectile = nil end
  MeshDeformation.reset("dent_barrel")
  Hud.set_text("dent_status", "Original barrel restored - no custom denting asset")
end

-- @HandleAction("dent_mass")
function Demo:on_dent_mass()
  self.mass = self.mass == 1 and 2 or self.mass == 2 and 8 or 1
  self:update_controls()
end

-- @HandleAction("dent_speed")
function Demo:on_dent_speed()
  self.speed = self.speed == 4 and 8 or self.speed == 8 and 16 or 4
  self:update_controls()
end

-- @HandleAction("dent_back")
function Demo:on_dent_back()
  Scene.load("scene://performance_3d_lab/main")
end

function Demo:on_update(dt)
  if Input.pressed("fire") then self:on_dent_hit() end
  if Input.pressed("reset") then self:on_dent_reset() end
  if Input.pressed("back") then self:on_dent_back() end
  if Input.pressed("dent_mass") then self:on_dent_mass() end
  if Input.pressed("dent_speed") then self:on_dent_speed() end
  if self.projectile then
    self.age = self.age + dt
    if self.age > 2 then Entity.destroy(self.projectile); self.projectile = nil end
  end
  local mouse = Input.mouse_down("left")
  if mouse and not self.mouse_was_down then
    local x, y = Input.mouse_position()
    local width, height = Input.viewport_size()
    local ray = Camera3D.screen_ray("camera", x, y, width, height)
    if ray then
      local o, d = ray.origin, ray.direction
      local hit = Physics3D.raycast(o[1], o[2], o[3], d[1], d[2], d[3], 20)
      if hit and hit.entity_id == "dent_barrel" then self:fire(o, d) end
    end
  end
  self.mouse_was_down = mouse
end

return Demo
