local Events = require("demi.events")
local Hud = require("demi.hud")
local Scene = require("demi.scene")
local Application = require("demi.application")
local Orbit = require("demi.ui.orbit")
local Showcase = {}
function Showcase:on_start()
  self.controls = {}
  local function keep(control) self.controls[#self.controls+1] = control; return control end
  local function feedback(text) Hud.set_text("demo.feedback", text) end
  if Hud.find("demo.settings") then
    self.quality = keep(Orbit.bind_dropdown("demo.settings.quality"))
    keep(Orbit.bind_choices("demo.settings.controls"))
  elseif Hud.find("demo.navigation") then
    keep(Orbit.bind_pagination("demo.navigation.pages", {pages=12}))
    keep(Orbit.bind_dropdown("demo.forms.quality"))
    keep(Orbit.bind_stepper("demo.forms.quantity", {minimum=1,maximum=9}))
    keep(Orbit.bind_tooltip("demo.forms.help", "demo.forms.tip"))
    keep(Orbit.bind_dismiss("demo.notice"))
    self.dialog = keep(Orbit.bind_dialog("demo.confirm", {on_change=function(confirmed)
      feedback(confirmed and "Expedition confirmed." or "Confirmation cancelled.")
    end}))
  elseif Hud.find("demo.pages") then
    keep(Orbit.bind_pagination("demo.pages", {pages=8}))
  end
  self.subscription = Events.subscribe("ui_event", function(event)
    if event.type ~= "submit" then return end
    if event.action == "menu.quit" then Application.quit()
    elseif event.action == "menu.continue" or event.action == "menu.new" then Scene.load("scene://orbit/inventory")
    elseif event.action == "menu.settings" then Scene.load("scene://orbit/main")
    elseif event.action == "orbit.icons" then Scene.load("scene://orbit/icons")
    elseif event.id == "demo.icon_action" then Scene.load("scene://orbit/main")
    elseif event.id == "demo.save" then Hud.set_text("demo.save.label", "Saved")
    elseif event.action == "orbit.components" then Scene.load("scene://orbit/components")
    elseif event.action == "orbit.showcase" then Scene.load("scene://orbit/main")
    elseif event.id == "demo.open_dialog" and self.dialog then self.dialog:open()
    elseif event.id == "demo.settings.apply" then
      feedback("Settings saved. Ready for the next mission.")
    elseif event.id == "demo.settings.reset" then
      Hud.set_value("demo.settings.volume", 0.72)
      Hud.set_value("demo.settings.music", 0.4)
      Hud.set_text("demo.settings.callsign", "")
      feedback("Defaults restored. A fresh start.")
    elseif event.id == "demo.actions.primary" then feedback("Expedition queued. Your crew is ready.")
    elseif event.id == "demo.actions.danger" then feedback("Mission cancelled. Your crew is safe.")
    end
  end)
end
function Showcase:on_destroy()
  if self.subscription then Events.unsubscribe(self.subscription) end
  for _,control in ipairs(self.controls or {}) do control:dispose() end
end
return Showcase
