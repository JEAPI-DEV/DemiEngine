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

-- @HandleAction("menu_button_network")
function Actions.show_network()
  ctx.view.show_network(ctx.menu, "CONNECTING TO MATCHMAKING")
  ctx.network.open()
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

-- @HandleAction("menu_network_back")
function Actions.network_back()
  ctx.network.back_to_main()
end

-- @HandleAction("menu_network_host")
function Actions.network_host()
  ctx.network.show_create()
end

-- @HandleAction("menu_network_join")
function Actions.network_join()
  ctx.network.refresh()
end

-- @HandleAction("network_refresh")
function Actions.network_refresh() ctx.network.refresh() end
-- @HandleAction("network_create_open")
function Actions.network_create_open() ctx.network.show_create() end
-- @HandleAction("network_page_previous")
function Actions.network_page_previous() ctx.network.previous_page() end
-- @HandleAction("network_page_next")
function Actions.network_page_next() ctx.network.next_page() end
-- @HandleAction("network_lobby_1")
function Actions.network_lobby_1() ctx.network.join_row(1) end
-- @HandleAction("network_lobby_2")
function Actions.network_lobby_2() ctx.network.join_row(2) end
-- @HandleAction("network_lobby_3")
function Actions.network_lobby_3() ctx.network.join_row(3) end
-- @HandleAction("network_lobby_4")
function Actions.network_lobby_4() ctx.network.join_row(4) end
-- @HandleAction("network_create_name")
function Actions.network_create_name() ctx.network.focus_name() end
-- @HandleAction("network_create_visibility")
function Actions.network_create_visibility() ctx.network.toggle_private() end
-- @HandleAction("network_create_password")
function Actions.network_create_password() ctx.network.focus_create_password() end
-- @HandleAction("network_create_submit")
function Actions.network_create_submit() ctx.network.submit_create() end
-- @HandleAction("network_form_cancel")
function Actions.network_form_cancel() ctx.network.cancel_form() end
-- @HandleAction("network_join_password")
function Actions.network_join_password() ctx.network.focus_join_password() end
-- @HandleAction("network_join_submit")
function Actions.network_join_submit() ctx.network.submit_join() end

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
