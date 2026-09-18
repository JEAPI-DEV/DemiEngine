local Input = require("demi.input")
local Entity = require("demi.entity")
local Timer = require("demi.timer")
local Scene = require("demi.scene")
local Hud = require("demi.hud")

---@demi_component
local Tower = {}

---@demi_property integer
---@range 2 8
Tower.columns = 8
---@demi_property integer
---@range 2 64
Tower.levels = 16
---@demi_property
---@range 1 200
Tower.projectile_mass = 60
---@demi_property
---@range 5 80
Tower.projectile_speed = 35
---@demi_property integer
---@range 0 128
Tower.solver_velocity_steps = 64
---@demi_property integer
---@range 0 128
Tower.solver_position_steps = 16

function Tower:on_start()
  self.shots = 0
  self.cooldown = 0
  self.cleanup_timers = {}
  local spacing = 0.70
  for level = 0, self.levels - 1 do
    for row = 0, self.columns - 1 do
      for column = 0, self.columns - 1 do
        local id = "tower_" .. level .. "_" .. row .. "_" .. column
        assert(Entity.create(id, {
          tags = level == self.levels - 1 and { "tower_barrel", "tower_top" } or { "tower_barrel" },
          components = {
          Transform3D = { position = {
            (column - (self.columns - 1) / 2) * spacing,
            -0.025 + level * 0.95,
            (row - (self.columns - 1) / 2) * spacing,
          } },
          MeshRenderer = { model = "asset://models/barrel", color = { 1, 1, 1, 1 } },
          ModelCollider3D = { asset = "asset://colliders/barrel" },
          Rigidbody3D = {
            body_type = "dynamic", friction = 0.8, restitution = 0,
            linear_damping = 0.1, angular_damping = 0.3,
            solver_velocity_steps = self.solver_velocity_steps,
            solver_position_steps = self.solver_position_steps,
          },
        } }))
      end
    end
  end
  Hud.set_text("tower_status", string.format("%d barrels | Let settle, then fire",
    self.columns * self.columns * self.levels))
end

function Tower:fire()
  if self.cooldown > 0 then return end
  self.cooldown = 0.75
  self.shots = self.shots + 1
  local id = "tower_projectile_" .. self.shots
  assert(Entity.create(id, { components = {
    Transform3D = { position = { 0, 0.8, 8 } },
    MeshRenderer = { shape = "sphere", size = { 1.6, 1.6, 1.6 }, color = { 1, 0.65, 0.15, 1 } },
    SphereCollider3D = { radius = 0.8 },
    Rigidbody3D = {
      body_type = "dynamic", mass = self.projectile_mass,
      velocity = { 0, 0, -self.projectile_speed }, continuous = true,
      linear_damping = 0, restitution = 0.1,
    },
  } }))
  Hud.set_text("tower_status", "Shots: " .. self.shots .. " | Collapse is gravity and collision only")
  local timer
  timer = Timer.after(8, function()
    Entity.destroy(id)
    self.cleanup_timers[timer] = nil
  end)
  self.cleanup_timers[timer] = true
end

function Tower:on_update(dt)
  self.cooldown = math.max(0, self.cooldown - dt)
  if Input.pressed("fire") then self:fire() end
  if Input.pressed("reset") then Scene.reload() end
  if Input.pressed("back") then Scene.load("scene://performance_3d_lab/main") end
end

function Tower:on_destroy()
  for timer in pairs(self.cleanup_timers or {}) do Timer.cancel(timer) end
end

-- @HandleAction("tower_fire")
function Tower:on_fire_action() self:fire() end
-- @HandleAction("tower_reset")
function Tower:on_reset_action() Scene.reload() end
-- @HandleAction("tower_back")
function Tower:on_back_action() Scene.load("scene://performance_3d_lab/main") end

return Tower
