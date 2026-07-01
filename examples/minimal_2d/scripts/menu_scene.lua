local main_menu = require("main_menu")

local MenuScene = {}

function MenuScene:on_start()
  main_menu.start()
end

function MenuScene:on_update(dt)
  main_menu.update(dt)
end

return MenuScene
