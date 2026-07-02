local View = {}

local EXTRA_JUMP_SLOT_IDS = { "extra_jump_slot_1", "extra_jump_slot_2", "extra_jump_slot_3" }
local EXTRA_JUMP_COIN_IDS = { "extra_jump_coin_1", "extra_jump_coin_2", "extra_jump_coin_3" }

local function show_group(group, visible)
  Hud.set_group_visible(group, visible)
end

function View.hide_menu()
  show_group("menu_base", false)
  show_group("menu_main", false)
  show_group("menu_levels", false)
  show_group("menu_network", false)
  show_group("menu_options", false)
  show_group("menu_sound", false)
  show_group("menu_video", false)
  show_group("menu_dropdown", false)
  show_group("game_over", false)
end

function View.set_game_hud_visible(visible)
  Hud.set_visible("points", visible)
  Hud.set_visible("fps", visible)
  for i = 1, 3 do
    Hud.set_visible(EXTRA_JUMP_SLOT_IDS[i], visible)
    Hud.set_visible(EXTRA_JUMP_COIN_IDS[i], false)
  end
end

function View.show_main(menu)
  menu.screen = "main"
  menu.dropdown_open = false
  show_group("menu_base", true)
  show_group("menu_main", true)
  show_group("menu_levels", false)
  show_group("menu_network", false)
  show_group("menu_options", false)
  show_group("menu_sound", false)
  show_group("menu_video", false)
  show_group("menu_dropdown", false)
end

function View.show_network(menu, status)
  menu.screen = "network"
  menu.network_join_pending = false
  menu.network_join_elapsed = 0.0
  menu.dropdown_open = false
  show_group("menu_base", true)
  show_group("menu_main", false)
  show_group("menu_levels", false)
  show_group("menu_network", true)
  show_group("menu_options", false)
  show_group("menu_sound", false)
  show_group("menu_video", false)
  show_group("menu_dropdown", false)
  if status ~= nil then
    Hud.set_text("menu_network_status", status)
  end
end

function View.show_levels(menu)
  menu.screen = "levels"
  menu.dropdown_open = false
  show_group("menu_base", true)
  show_group("menu_main", false)
  show_group("menu_levels", true)
  show_group("menu_network", false)
  show_group("menu_options", false)
  show_group("menu_sound", false)
  show_group("menu_video", false)
  show_group("menu_dropdown", false)
end

function View.show_options(menu, update_volume_hud, update_video_hud)
  menu.screen = "options"
  show_group("menu_base", true)
  show_group("menu_main", false)
  show_group("menu_levels", false)
  show_group("menu_network", false)
  show_group("menu_options", true)
  if menu.active_tab == "sound" then
    Hud.set_color("menu_tab_sound", 0.24, 0.27, 0.46, 0.94)
    Hud.set_color("menu_tab_video", 0.12, 0.14, 0.26, 0.62)
  else
    Hud.set_color("menu_tab_sound", 0.12, 0.14, 0.26, 0.62)
    Hud.set_color("menu_tab_video", 0.24, 0.27, 0.46, 0.94)
  end
  show_group("menu_sound", menu.active_tab == "sound")
  show_group("menu_video", menu.active_tab == "video")
  show_group("menu_dropdown", menu.active_tab == "video" and menu.dropdown_open)
  update_volume_hud()
  update_video_hud()
end

function View.show_group(group, visible)
  show_group(group, visible)
end

return View
