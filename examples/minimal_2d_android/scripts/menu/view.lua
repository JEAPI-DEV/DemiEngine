local View = {}

local EXTRA_JUMP_SLOT_IDS = { "extra_jump_slot_1", "extra_jump_slot_2", "extra_jump_slot_3" }
local EXTRA_JUMP_COIN_IDS = { "extra_jump_coin_1", "extra_jump_coin_2", "extra_jump_coin_3" }

local function show_group(group, visible)
  Hud.set_group_visible(group, visible)
end

local function hide_network_groups()
  show_group("menu_network", false)
  show_group("network_browser", false)
  show_group("network_empty", false)
  show_group("network_create", false)
  show_group("network_password", false)
end

function View.hide_menu()
  show_group("menu_base", false)
  show_group("menu_main", false)
  show_group("menu_levels", false)
  hide_network_groups()
  show_group("menu_options", false)
  show_group("menu_sound", false)
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
  show_group("menu_base", true)
  show_group("menu_main", true)
  show_group("menu_levels", false)
  hide_network_groups()
  show_group("menu_options", false)
  show_group("menu_sound", false)
end

function View.show_levels(menu)
  menu.screen = "levels"
  show_group("menu_base", true)
  show_group("menu_main", false)
  show_group("menu_levels", true)
  hide_network_groups()
  show_group("menu_options", false)
  show_group("menu_sound", false)
end

function View.show_network(menu, status)
  menu.screen = "network"
  menu.network_join_pending = false
  menu.network_join_elapsed = 0.0
  show_group("menu_base", true)
  show_group("menu_main", false)
  show_group("menu_levels", false)
  show_group("menu_network", true)
  show_group("network_browser", true)
  show_group("network_empty", true)
  show_group("network_create", false)
  show_group("network_password", false)
  show_group("menu_options", false)
  show_group("menu_sound", false)
  if status ~= nil then
    Hud.set_text("menu_network_status", status)
  end
end

function View.show_options(menu, update_volume_hud)
  menu.screen = "options"
  show_group("menu_base", true)
  show_group("menu_main", false)
  show_group("menu_levels", false)
  hide_network_groups()
  show_group("menu_options", true)
  show_group("menu_sound", true)
  update_volume_hud()
end

function View.show_group(group, visible)
  show_group(group, visible)
end

return View
