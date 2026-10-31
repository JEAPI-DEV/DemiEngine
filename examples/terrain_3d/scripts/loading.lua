local Hud = require("demi.hud")
local Scene = require("demi.scene")
local Input = require("demi.input")
local Application = require("demi.application")

---@demi_component
local Loading = {}
---@demi_property
Loading.target_scene = "scene://terrain_3d/main"
---@demi_property
Loading.title = "SILVERMERE"

function Loading:on_create()
  Hud.set_text("loading_title", self.title)
  self.activity = 0
  self.started = false
  self.failed = false
  self.activation_requested = false
end

function Loading:show_error(message)
  self.failed = true
  Hud.set_text("loading_stage", "The landscape could not be prepared")
  Hud.set_text("loading_error", message)
end

function Loading:on_update(dt)
  if Input.key_pressed("escape") then
    Application.quit()
    return
  end
  self.activity = self.activity + dt
  if self.failed or self.activation_requested then
    return
  end
  if not self.started then
    self.started = true
    if not Scene.prepare(self.target_scene) then
      self:show_error(Scene.error())
    end
    return
  end

  local progress = Scene.progress()
  Hud.set_value("loading_progress", progress)
  if progress == 0 then
    Hud.set_text("loading_stage", "Preparing the landscape" .. string.rep(".", math.floor(self.activity * 2) % 4))
  else
    Hud.set_text("loading_stage", "Preparing scene resources")
  end
  local error = Scene.error()
  if error ~= "" then
    self:show_error(error)
    return
  end
  if Scene.is_prepared() then
    Hud.set_value("loading_progress", 1)
    Hud.set_text("loading_stage", "Ready")
    self.activation_requested = Scene.activate()
    if not self.activation_requested then
      self:show_error(Scene.error())
    end
  end
end

return Loading
