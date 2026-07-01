local state = require("game_state")
local replication = require("network_replication")

local Menu = {
  screen = "main",
  active_tab = "sound",
  dropdown_open = false,
  volume = 1.0,
  window_mode = "windowed",
  network_ip = "127.0.0.1",
  network_port = 39420,
  network_join_pending = false,
  network_join_elapsed = 0.0,
  network_join_timeout = 4.0,
  key_was_down = {},
  enter_was_down = false,
}

local PLAYER_ID = "ent_player"
local SETTINGS_SLOT = "settings"
local SCENE_MENU = "scene://minimal_2d_networking/menu"
local SCENE_PLATFORMER = "scene://minimal_2d_networking/platformer"
local SCENE_SPIRAL = "scene://minimal_2d_networking/spiral"
local EXTRA_JUMP_SLOT_IDS = { "extra_jump_slot_1", "extra_jump_slot_2", "extra_jump_slot_3" }
local EXTRA_JUMP_COIN_IDS = { "extra_jump_coin_1", "extra_jump_coin_2", "extra_jump_coin_3" }

local function show_group(group, visible)
  Hud.set_group_visible(group, visible)
end

local function volume_label()
  return tostring(math.floor(Menu.volume * 100.0 + 0.5)) .. " PCT"
end

local function mode_label(value)
  if value == "borderless" then
    return "BORDERLESS"
  end
  if value == "fullscreen" then
    return "FULLSCREEN"
  end
  return "WINDOWED"
end

local function update_volume_hud()
  local fill_width = 308.0 * Menu.volume
  Hud.set_rect("menu_volume_fill", 326.0, 305.0, fill_width, 16.0)
  Hud.set_rect("menu_volume_knob", 326.0 + fill_width - 4.0, 299.0, 8.0, 28.0)
  Hud.set_text("menu_volume_value", volume_label())
end

local function update_video_hud()
  Hud.set_text("menu_window_mode_label", mode_label(Menu.window_mode))
  Hud.set_text("menu_dropdown_arrow", Menu.dropdown_open and "-" or "+")
  Hud.set_visible("menu_back", not Menu.dropdown_open)
end

local function update_network_hud(status)
  Hud.set_text("menu_network_ip_value", Menu.network_ip == "" and "_" or Menu.network_ip)
  if status ~= nil then
    Hud.set_text("menu_network_status", status)
  end
end

local function set_volume(volume)
  Menu.volume = math.max(0.0, math.min(1.0, volume))
  Audio.set_master_volume(Menu.volume)
  Save.set_number(SETTINGS_SLOT, "master_volume", Menu.volume)
  update_volume_hud()
end

local function set_window_mode(mode)
  Menu.window_mode = mode
  Runtime.set_window_mode(mode)
  Save.set_string(SETTINGS_SLOT, "window_mode", mode)
  Menu.dropdown_open = false
  update_video_hud()
  show_group("menu_dropdown", false)
end

local function hide_menu()
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

local function set_game_hud_visible(visible)
  Hud.set_visible("points", visible)
  for i = 1, 3 do
    Hud.set_visible(EXTRA_JUMP_SLOT_IDS[i], visible)
    Hud.set_visible(EXTRA_JUMP_COIN_IDS[i], false)
  end
end

local function show_main()
  Menu.screen = "main"
  Menu.dropdown_open = false
  show_group("menu_base", true)
  show_group("menu_main", true)
  show_group("menu_levels", false)
  show_group("menu_network", false)
  show_group("menu_options", false)
  show_group("menu_sound", false)
  show_group("menu_video", false)
  show_group("menu_dropdown", false)
end

local function show_network()
  Menu.screen = "network"
  Menu.network_join_pending = false
  Menu.network_join_elapsed = 0.0
  Menu.dropdown_open = false
  show_group("menu_base", true)
  show_group("menu_main", false)
  show_group("menu_levels", false)
  show_group("menu_network", true)
  show_group("menu_options", false)
  show_group("menu_sound", false)
  show_group("menu_video", false)
  show_group("menu_dropdown", false)
  update_network_hud(Network.available() and "PORT " .. tostring(Menu.network_port) or "NETWORK BUILD DISABLED")
end

local function show_levels()
  Menu.screen = "levels"
  Menu.dropdown_open = false
  show_group("menu_base", true)
  show_group("menu_main", false)
  show_group("menu_levels", true)
  show_group("menu_network", false)
  show_group("menu_options", false)
  show_group("menu_sound", false)
  show_group("menu_video", false)
  show_group("menu_dropdown", false)
end

local function show_options()
  Menu.screen = "options"
  show_group("menu_base", true)
  show_group("menu_main", false)
  show_group("menu_levels", false)
  show_group("menu_network", false)
  show_group("menu_options", true)
  if Menu.active_tab == "sound" then
    Hud.set_color("menu_tab_sound", 0.24, 0.27, 0.46, 0.94)
    Hud.set_color("menu_tab_video", 0.12, 0.14, 0.26, 0.62)
  else
    Hud.set_color("menu_tab_sound", 0.12, 0.14, 0.26, 0.62)
    Hud.set_color("menu_tab_video", 0.24, 0.27, 0.46, 0.94)
  end
  show_group("menu_sound", Menu.active_tab == "sound")
  show_group("menu_video", Menu.active_tab == "video")
  show_group("menu_dropdown", Menu.active_tab == "video" and Menu.dropdown_open)
  update_volume_hud()
  update_video_hud()
end

local function show_tab(tab)
  Menu.active_tab = tab
  if tab == "sound" then
    Menu.dropdown_open = false
  end
  show_options()
end

local function select_level(scene_id, level)
  state.pending_scene = scene_id
  state.level = level
  state.auto_start = true
  state.fall_floor_y = nil
  state.game_over = false
  state.game_over_pending = false
  Scene.load(scene_id)
end

function Menu.start_game()
  select_level(SCENE_PLATFORMER, 1)
end

function Menu.host_game()
  Menu.network_join_pending = false
  if replication.host(Menu.network_port) then
    update_network_hud("HOSTING ON PORT " .. tostring(Menu.network_port))
    select_level(SCENE_PLATFORMER, 1)
  else
    update_network_hud("HOST FAILED - ENABLE NETWORK BUILD")
  end
end

function Menu.join_game()
  if Menu.network_ip == "" then
    update_network_hud("ENTER SERVER IP")
    return
  end
  if replication.connect(Menu.network_ip, Menu.network_port) then
    Menu.network_join_pending = true
    Menu.network_join_elapsed = 0.0
    update_network_hud("CONNECTING TO " .. Menu.network_ip)
  else
    Menu.network_join_pending = false
    update_network_hud("JOIN FAILED - CHECK IP OR BUILD")
  end
end

function Menu.apply_settings()
  Menu.volume = Save.get_number(SETTINGS_SLOT, "master_volume", Audio.get_master_volume())
  Menu.window_mode = Save.get_string(SETTINGS_SLOT, "window_mode", Runtime.get_window_mode())
  Audio.set_master_volume(Menu.volume)
  Runtime.set_window_mode(Menu.window_mode)
end

function Menu.start()
  state.menu_open = true
  state.game_over = false
  state.game_over_pending = false
  Menu.screen = "main"
  Menu.active_tab = "sound"
  Menu.dropdown_open = false
  Menu.key_was_down = {}
  Menu.enter_was_down = true
  Menu.apply_settings()
  Runtime.set_physics_enabled(false)
  set_game_hud_visible(false)
  show_group("game_over", false)
  show_main()
end

function Menu.begin_active_level()
  hide_menu()
  state.menu_open = false
  state.game_started = true
  state.game_over = false
  state.game_over_pending = false
  Runtime.set_physics_enabled(true)
  set_game_hud_visible(true)
  local px, py = Entity.get_position(PLAYER_ID)
  if px ~= nil and py ~= nil then
    state.respawn_x = px
    state.respawn_y = py
  end
  Entity.set_position(PLAYER_ID, state.respawn_x, state.respawn_y)
  Rigidbody2D.set_velocity(PLAYER_ID, 0.0, 0.0)
end

function Menu.show_game_over(points)
  state.menu_open = true
  state.game_started = false
  state.game_over = true
  state.game_over_pending = false
  Runtime.set_physics_enabled(false)
  set_game_hud_visible(false)
  hide_menu()
  Hud.set_text("game_over_points", "POINTS: " .. tostring(points))
  show_group("game_over", true)
  Menu.screen = "game_over"
end

function Menu.back_to_main_menu()
  replication.disconnect()
  Menu.network_join_pending = false
  state.auto_start = false
  state.game_over = false
  state.game_over_pending = false
  state.extra_jumps = 0
  state.level = 1
  Scene.load(SCENE_MENU)
end

function Menu.handle_click(id)
  if id == "menu_button_levels" then
    show_levels()
  elseif id == "menu_button_network" then
    show_network()
  elseif id == "menu_button_level_1" then
    select_level(SCENE_PLATFORMER, 1)
  elseif id == "menu_button_level_2" then
    select_level(SCENE_SPIRAL, 2)
  elseif id == "menu_levels_back" then
    show_main()
  elseif id == "menu_button_options" then
    show_options()
  elseif id == "menu_network_host" then
    Menu.host_game()
  elseif id == "menu_network_join" then
    Menu.join_game()
  elseif id == "menu_network_back" then
    if Menu.network_join_pending then
      replication.disconnect()
      Menu.network_join_pending = false
    end
    show_main()
  elseif id == "menu_button_quit" then
    Runtime.quit()
  elseif id == "menu_back" then
    show_main()
  elseif id == "menu_tab_sound" then
    show_tab("sound")
  elseif id == "menu_tab_video" then
    show_tab("video")
  elseif id == "menu_volume_minus" then
    set_volume(Menu.volume - 0.10)
  elseif id == "menu_volume_plus" then
    set_volume(Menu.volume + 0.10)
  elseif id == "menu_window_dropdown" then
    Menu.dropdown_open = not Menu.dropdown_open
    update_video_hud()
    show_group("menu_dropdown", Menu.dropdown_open)
  elseif id == "menu_window_mode_windowed" then
    set_window_mode("windowed")
  elseif id == "menu_window_mode_borderless" then
    set_window_mode("borderless")
  elseif id == "menu_window_mode_fullscreen" then
    set_window_mode("fullscreen")
  elseif id == "game_over_back" then
    Menu.back_to_main_menu()
  end
end

local function update_network_connection(dt)
  if not Menu.network_join_pending then
    return
  end

  Menu.network_join_elapsed = Menu.network_join_elapsed + dt
  local events = replication.process_events()
  if events.connected or replication.is_connected() then
    Menu.network_join_pending = false
    update_network_hud("CONNECTED")
    select_level(SCENE_PLATFORMER, 1)
    return
  end

  if events.disconnected then
    Menu.network_join_pending = false
    replication.disconnect()
    update_network_hud("JOIN FAILED - NO HOST")
    return
  end

  if Menu.network_join_elapsed >= Menu.network_join_timeout then
    Menu.network_join_pending = false
    replication.disconnect()
    update_network_hud("JOIN TIMED OUT")
  end
end

local function pressed_once(key)
  local down = Input.is_down(key)
  local pressed = down and not Menu.key_was_down[key]
  Menu.key_was_down[key] = down
  return pressed
end

local function append_ip_char(value)
  if string.len(Menu.network_ip) >= 15 then
    return
  end
  Menu.network_ip = Menu.network_ip .. value
  update_network_hud(nil)
end

local function update_network_input()
  for digit = 0, 9 do
    local key = tostring(digit)
    if pressed_once(key) then
      append_ip_char(key)
    end
  end

  if pressed_once(".") or pressed_once("period") then
    append_ip_char(".")
  end

  if pressed_once("backspace") and string.len(Menu.network_ip) > 0 then
    Menu.network_ip = string.sub(Menu.network_ip, 1, string.len(Menu.network_ip) - 1)
    update_network_hud(nil)
  end

  if pressed_once("delete") then
    Menu.network_ip = ""
    update_network_hud(nil)
  end
end

function Menu.update(dt)
  local enter_down = Input.is_down("return") or Input.is_down("space")
  local enter_pressed = enter_down and not Menu.enter_was_down

  if Menu.screen == "main" and enter_pressed then
    Menu.start_game()
  end

  if Menu.screen == "levels" then
    if Input.is_down("1") then
      select_level(SCENE_PLATFORMER, 1)
    elseif Input.is_down("2") then
      select_level(SCENE_SPIRAL, 2)
    elseif Input.is_down("escape") then
      show_main()
    end
  end

  if Menu.screen == "network" then
    update_network_input()
    update_network_connection(dt)
    if Input.is_down("escape") then
      if Menu.network_join_pending then
        replication.disconnect()
        Menu.network_join_pending = false
      end
      show_main()
    end
  end

  if Menu.screen == "options" and Menu.active_tab == "sound" then
    if Input.is_down("left") then
      set_volume(Menu.volume - 0.02)
    elseif Input.is_down("right") then
      set_volume(Menu.volume + 0.02)
    end
  end

  Menu.enter_was_down = enter_down
end

return Menu
