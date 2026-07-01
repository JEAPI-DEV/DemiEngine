local main_menu = require("main_menu")

local MenuButton = {}

function MenuButton:on_ui_click(event)
  main_menu.handle_click(event.id)
end

return MenuButton
