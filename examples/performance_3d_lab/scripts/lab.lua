local Input = require("demi.input")
local Application = require("demi.application")
local Entity = require("demi.entity")
local Transform3D = require("demi.transform3d")
local Timer = require("demi.timer")
local Scene = require("demi.scene")

---@demi_component
local Lab = {}

---@demi_property integer
---@range 1 5000
Lab.count = 250
---@demi_property
---@options mesh, rigid, pile
Lab.workload = "rigid"
---@demi_property
---@options primitives, barrel
Lab.geometry = "primitives"
---@demi_property
Lab.varied = false
---@demi_property
---@range 0 600
Lab.duration_seconds = 0.0

function Lab:on_start()
  if self.duration_seconds > 0 then
    Timer.after(self.duration_seconds, function() Application.quit() end)
  end
  self.time = 0
  self.instances = {}
  for index = 1, self.count do
    local slot = index - 1
    local spacing = self.workload == "pile" and 0.85 or 1.2
    local x = (slot % 40 - 19.5) * spacing
    local y = 2 + math.floor(slot / 400) * 1.2
    local z = (math.floor(slot / 40) % 10 - 4.5) * spacing
    local id = "probe_" .. index
    local sphere = not self.varied or index % 2 == 0
    local barrel = self.geometry == "barrel"
    local components = {
      Transform3D = { position = { x, y, z } },
      MeshRenderer = {
        shape = sphere and "sphere" or "cube", size = { 0.7, 0.7, 0.7 },
        color = self.varied and { (index % 8) / 8, 0.5, 0.8, 1 } or { 0.4, 0.65, 0.95, 1 },
      },
    }
    if barrel then
      components.MeshRenderer = {
        model = "asset://models/barrel",
        color = self.varied and { (index % 8) / 8, 0.5, 0.8, 1 } or { 1, 1, 1, 1 },
      }
      if self.workload == "pile" then
        -- Start slightly off-axis to exercise tipping/rolling. The small tilt
        -- still fits inside the unchanged spawn spacing without overlap.
        components.Transform3D.rotation = {
          (index % 3 - 1) * 0.1, (index % 7) * 0.15, (index % 5 - 2) * 0.05,
        }
      end
    end
    if self.workload ~= "mesh" then
      components.Rigidbody3D = {
        body_type = "dynamic", use_gravity = self.workload == "pile",
        velocity = self.workload == "rigid" and { 0, index % 2 == 0 and 0.4 or -0.4, 0 } or { 0, 0, 0 },
        linear_damping = 0, angular_damping = 0, allow_sleep = self.workload == "pile",
        continuous = false, report_contacts = true,
      }
      if not barrel then
        if sphere then components.SphereCollider3D = { radius = 0.35 }
        else components.BoxCollider3D = { size = { 0.7, 0.7, 0.7 } } end
      else
        components.ModelCollider3D = { asset = "asset://colliders/barrel" }
      end
    end
    assert(Entity.create(id, { components = components }))
    self.instances[index] = { id = id, x = x, y = y, z = z }
  end
  print("PERF_LAB workload=" .. self.workload .. " count=" .. self.count .. " geometry=" .. self.geometry .. " varied=" .. tostring(self.varied))
end

function Lab:on_fixed_update(dt)
  self.time = self.time + dt
  if self.workload ~= "mesh" then return end
  local offset = math.sin(self.time) * 0.5
  for _, instance in ipairs(self.instances) do
    Transform3D.set_position(instance.id, instance.x, instance.y + offset, instance.z)
  end
end

function Lab:on_update()
  if Input.pressed("dent_demo") then self:on_open_denting() end
  if Input.pressed("tower") then Scene.load("scene://performance_3d_lab/tower") end
end

-- @HandleAction("open_tower")
function Lab:on_open_tower()
  Scene.load("scene://performance_3d_lab/tower")
end

-- @HandleAction("open_denting")
function Lab:on_open_denting()
  Scene.load("scene://performance_3d_lab/denting")
end

return Lab
