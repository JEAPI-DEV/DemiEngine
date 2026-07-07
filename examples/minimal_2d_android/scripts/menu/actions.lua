local Actions = {}

local ctx = nil

function Actions.bind(context)
  ctx = context
end

-- @HandleAction("menu_button_levels")
function Actions.show_levels()
  ctx.view.show_levels(ctx.menu)
end

-- @HandleAction("menu_button_options")
function Actions.show_options()
  ctx.show_options()
end

-- @HandleAction("menu_button_level_1")
function Actions.level_1()
  ctx.select_level(ctx.scenes.platformer, 1)
end

-- @HandleAction("menu_button_level_2")
function Actions.level_2()
  ctx.select_level(ctx.scenes.spiral, 2)
end

-- @HandleAction("menu_levels_back")
-- @HandleAction("menu_back")
function Actions.show_main()
  ctx.view.show_main(ctx.menu)
end

-- @HandleAction("menu_button_quit")
function Actions.quit()
  Runtime.quit()
end

-- @HandleAction("menu_volume_minus")
function Actions.volume_down()
  ctx.settings.set_volume(ctx.menu.volume - 0.10)
end

-- @HandleAction("menu_volume_plus")
function Actions.volume_up()
  ctx.settings.set_volume(ctx.menu.volume + 0.10)
end

-- @HandleAction("game_retry")
function Actions.retry_game()
  ctx.retry_game()
end

-- @HandleAction("game_over_back")
function Actions.back_to_main_menu()
  ctx.back_to_main_menu()
end

return Actions
