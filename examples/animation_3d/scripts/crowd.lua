---@demi_component
local Crowd = {}

---@demi_property integer
---@range 1 2000
Crowd.count = 64
---@demi_property
Crowd.frozen = false
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
    assert(Entity.create("character_" .. index, { components = {
      Transform3D = { position = {
        (slot % columns - (columns - 1) / 2) * 2,
        0, (math.floor(slot / columns) - (columns - 1) / 2) * 2,
      } },
      MeshRenderer = { model = "asset://animation_lib/ual1_standard" },
      -- Distinct phases avoid measuring a synchronized-pose special case.
      -- Frozen retains the same skinned draw path, only playback is stopped.
      AnimationPlayer3D = {
        clip_name = "Walk_Loop", playing = not self.frozen,
        time = (slot % 17) / 17, speed = 1 + (slot % 5) * 0.03,
      },
    } }))
  end
  Hud.set_text("crowd_status", tostring(self.count) .. " CHARACTERS | " ..
    (self.frozen and "FROZEN CONTROL" or "INDEPENDENT WALK PLAYBACK"))
  print("ANIMATION_CROWD count=" .. self.count .. " frozen=" .. tostring(self.frozen))
end

function Crowd:on_update()
  if Input.pressed("animation_back") then self:on_back() end
end

-- @HandleAction("animation_back")
function Crowd:on_back()
  Scene.load("scene://animation_3d/main")
end

return Crowd
