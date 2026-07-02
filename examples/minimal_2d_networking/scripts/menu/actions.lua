local Actions = {}

local ctx = nil

function Actions.bind(context)
  ctx = context
end

-- @HandleAction("menu_button_levels")
function Actions.show_levels()
  ctx.view.show_levels(ctx.menu)
end

-- @HandleAction("menu_button_network")
function Actions.show_network()
  ctx.view.show_network(ctx.menu, Network.available() and "PORT " .. tostring(ctx.menu.network_port) or "NETWORK BUILD DISABLED")
  ctx.network.update_hud(nil)
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

-- @HandleAction("menu_network_back")
function Actions.network_back()
  ctx.network.back_to_main()
end

-- @HandleAction("menu_button_quit")
function Actions.quit()
  Runtime.quit()
end

-- @HandleAction("menu_network_host")
function Actions.host_game()
  ctx.network.host_game()
end

-- @HandleAction("menu_network_join")
function Actions.join_game()
  ctx.network.join_game()
end

-- @HandleAction("menu_tab_sound")
function Actions.sound_tab()
  ctx.show_tab("sound")
end

-- @HandleAction("menu_tab_video")
function Actions.video_tab()
  ctx.show_tab("video")
end

-- @HandleAction("menu_volume_minus")
function Actions.volume_down()
  ctx.settings.set_volume(ctx.menu.volume - 0.10)
end

-- @HandleAction("menu_volume_plus")
function Actions.volume_up()
  ctx.settings.set_volume(ctx.menu.volume + 0.10)
end

-- @HandleAction("menu_window_dropdown")
function Actions.window_dropdown()
  ctx.settings.cancel_max_fps_edit()
  ctx.menu.dropdown_open = not ctx.menu.dropdown_open
  ctx.settings.update_video_hud()
  ctx.view.show_group("menu_dropdown", ctx.menu.dropdown_open)
end

-- @HandleAction("menu_window_mode_windowed")
function Actions.windowed()
  ctx.settings.set_window_mode("windowed")
end

-- @HandleAction("menu_window_mode_borderless")
function Actions.borderless()
  ctx.settings.set_window_mode("borderless")
end

-- @HandleAction("menu_window_mode_fullscreen")
function Actions.fullscreen()
  ctx.settings.set_window_mode("fullscreen")
end

-- @HandleAction("menu_max_fps_minus")
function Actions.fps_down()
  ctx.settings.cancel_max_fps_edit()
  ctx.settings.change_max_fps(-15)
end

-- @HandleAction("menu_max_fps_plus")
function Actions.fps_up()
  ctx.settings.cancel_max_fps_edit()
  ctx.settings.change_max_fps(15)
end

-- @HandleAction("menu_max_fps_input")
function Actions.fps_input()
  ctx.settings.begin_max_fps_edit()
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
