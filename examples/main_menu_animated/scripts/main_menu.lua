local GuiAnimation = require("demi.gui_animation")

local MainMenu = {}

function MainMenu:on_create()
  self.timeline = GuiAnimation.new()
end

function MainMenu:on_start()
  self.timeline:play("walker", 8.0, function(progress, elapsed)
    Hud.set_position("walker", -104.0 + 1064.0 * progress, 220.0)
    Hud.set_image_animation_frame("walker", "asset://walk/warrior", math.floor(elapsed * 10.0))
  end, true)
end

function MainMenu:on_update(dt)
  self.timeline:update(dt)
end

return MainMenu
