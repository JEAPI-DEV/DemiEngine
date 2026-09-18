local Input = require("demi.input")
local Application = require("demi.application")
local Entity = require("demi.entity")
local Timer = require("demi.timer")
local Scene = require("demi.scene")
local Hud = require("demi.hud")

---@demi_component
local Crowd = {}

---@demi_property integer
---@range 1 2000
Crowd.count = 64
---@demi_property
Crowd.frozen = false
---@demi_property
Crowd.mixed = false
---@demi_property
---@range 0 240
Crowd.visual_rate = 0
---@demi_property
---@range 0 1000
Crowd.visual_distance = 30
---@demi_property
---@range 0 600
Crowd.duration_seconds = 0

function Crowd:on_start()
  if self.duration_seconds > 0 then
    Timer.after(self.duration_seconds, function() Application.quit() end)
  end
  local columns = math.ceil(math.sqrt(self.count))
  for index = 1, self.count do
    local slot = index - 1
    local character = not self.mixed or index % 2 == 1
    local components = {
      Transform3D = { position = {
        (slot % columns - (columns - 1) / 2) * 2,
        self.mixed and 2 or 0, (math.floor(slot / columns) - (columns - 1) / 2) * 2,
      } },
      MeshRenderer = { model = "asset://animation_lib/ual1_standard" },
      -- Distinct phases avoid measuring a synchronized-pose special case.
      -- Frozen retains the same skinned draw path, only playback is stopped.
      AnimationPlayer3D = {
        clip_name = "Walk_Loop", playing = not self.frozen,
        time = (slot % 17) / 17, speed = 1 + (slot % 5) * 0.03,
        visual_update_rate = self.visual_rate, visual_update_distance = self.visual_distance,
      },
    }
    if self.mixed then
      components.Rigidbody3D = {
        body_type = "dynamic", velocity = {index % 2 == 0 and -0.2 or 0.2, 0, 0},
        linear_damping = 0, friction = 0, allow_sleep = false,
        lock_rotation_x = true, lock_rotation_z = true, report_contacts = true,
      }
      if character then
        components.CapsuleCollider3D = { radius = 0.28, height = 1.6, offset = {0, 0.8, 0} }
      else
        components.AnimationPlayer3D = nil
        components.MeshRenderer = { shape = "cube", size = {0.7, 0.7, 0.7},
          color = {0.25 + (index % 4) * 0.15, 0.4, 0.7, 1} }
        components.BoxCollider3D = { size = {0.7, 0.7, 0.7} }
      end
    end
    assert(Entity.create("character_" .. index, { components = components }))
  end
  Hud.set_text("crowd_status", tostring(self.count) .. " CHARACTERS | " ..
    (self.frozen and "FROZEN CONTROL" or "INDEPENDENT WALK PLAYBACK"))
  print("ANIMATION_CROWD count=" .. self.count .. " frozen=" .. tostring(self.frozen))
  if self.mixed then Hud.set_text("crowd_status", self.count .. " MIXED OBJECTS | ANIMATION + ACTIVE COLLISIONS") end
  if self.visual_rate > 0 and not self.frozen then
    Hud.set_text("crowd_status", Hud.get_text("crowd_status") .. " | FAR POSES " .. self.visual_rate .. " HZ")
  end
end

function Crowd:on_update()
  if Input.pressed("animation_back") then self:on_back() end
end

-- @HandleAction("animation_back")
function Crowd:on_back()
  Scene.load("scene://animation_3d/main")
end

return Crowd
