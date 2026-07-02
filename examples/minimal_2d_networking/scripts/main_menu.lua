local state = require("game_state")
local replication = require("network_replication")

local Menu = {
  screen = "main",
  active_tab = "sound",
  dropdown_open = false,
  volume = 1.0,
  window_mode = "windowed",
  max_fps = 0,
  max_fps_editing = false,
  max_fps_buffer = "",
  network_ip = "127.0.0.1",
  network_port = 39420,
  network_level = 1,
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

local function max_fps_label()
  if Menu.max_fps_editing then
    return Menu.max_fps_buffer == "" and "_" or Menu.max_fps_buffer .. "_"
  end
  if Menu.max_fps <= 0 then
    return "UNLIMITED"
  end
  return tostring(Menu.max_fps)
end

local function update_volume_hud()
  local fill_width = 308.0 * Menu.volume
  Hud.set_rect("menu_volume_fill", 326.0, 305.0, fill_width, 16.0)
  Hud.set_rect("menu_volume_knob", 326.0 + fill_width - 4.0, 299.0, 8.0, 28.0)
  Hud.set_text("menu_volume_value", volume_label())
end

local function update_video_hud()
  Hud.set_text("menu_max_fps_label", "Max-FPS:")
  Hud.set_text("menu_window_mode_label", mode_label(Menu.window_mode))
  Hud.set_text("menu_dropdown_arrow", Menu.dropdown_open and "-" or "+")
  Hud.set_text("menu_max_fps_value", max_fps_label())
  Hud.set_color("menu_max_fps_input", Menu.max_fps_editing and 0.12 or 0.05, Menu.max_fps_editing and 0.16 or 0.06, Menu.max_fps_editing and 0.28 or 0.13, 0.94)
  Hud.set_visible("menu_back", not Menu.dropdown_open)
end

local function update_network_hud(status)
  Hud.set_text("menu_network_ip_value", Menu.network_ip == "" and "_" or Menu.network_ip)
  Hud.set_text("menu_network_level", "HOST LEVEL " .. tostring(Menu.network_level) .. "  -  F1/F2")
  if status ~= nil then
    Hud.set_text("menu_network_status", status)
  end
end

local function scene_for_level(level)
  if level == 2 then
    return SCENE_SPIRAL
  end
  return SCENE_PLATFORMER
end

local function active_scene()
  return scene_for_level(state.level or 1)
end

local function multiplayer_active()
  local session = replication.current_session()
  return session ~= nil and session.scene_id ~= nil
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

local function set_max_fps(value)
  if value <= 0 then
    Menu.max_fps = 0
  else
    Menu.max_fps = math.max(15, math.min(1000, math.floor(value + 0.5)))
  end
  Runtime.set_max_fps(Menu.max_fps)
  Save.set_number(SETTINGS_SLOT, "max_fps", Menu.max_fps)
  update_video_hud()
end

local function begin_max_fps_edit()
  Menu.max_fps_editing = true
  Menu.max_fps_buffer = Menu.max_fps <= 0 and "" or tostring(Menu.max_fps)
  update_video_hud()
end

local function commit_max_fps_edit()
  if not Menu.max_fps_editing then
    return
  end
  local value = tonumber(Menu.max_fps_buffer)
  Menu.max_fps_editing = false
  Menu.max_fps_buffer = ""
  set_max_fps(value or 0)
end

local function cancel_max_fps_edit()
  Menu.max_fps_editing = false
  Menu.max_fps_buffer = ""
  update_video_hud()
end

local function change_max_fps(delta)
  if Menu.max_fps <= 0 and delta > 0 then
    set_max_fps(60)
    return
  end
  if Menu.max_fps <= 15 and delta < 0 then
    set_max_fps(0)
    return
  end
  set_max_fps(Menu.max_fps + delta)
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
  Hud.set_visible("fps", visible)
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

-- @HandleAction("menu_network_host")
function Menu.host_game()
  Menu.network_join_pending = false
  if replication.host(Menu.network_port) then
    local scene_id = scene_for_level(Menu.network_level)
    replication.start_session({
      scene_id = scene_id,
      level = Menu.network_level,
      seed = 1000 + Menu.network_level,
    })
    update_network_hud("HOSTING LEVEL " .. tostring(Menu.network_level) .. " ON PORT " .. tostring(Menu.network_port))
    select_level(scene_id, Menu.network_level)
  else
    update_network_hud("HOST FAILED - ENABLE NETWORK BUILD")
  end
end

-- @HandleAction("menu_network_join")
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
  Menu.max_fps = Save.get_number(SETTINGS_SLOT, "max_fps", Runtime.get_max_fps())
  Audio.set_master_volume(Menu.volume)
  Runtime.set_window_mode(Menu.window_mode)
  Runtime.set_max_fps(Menu.max_fps)
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
  Hud.set_button_label("game_retry", multiplayer_active() and "RESPAWN" or "TRY AGAIN")
  show_group("game_over", true)
  Menu.screen = "game_over"
end

-- @HandleAction("game_retry")
function Menu.retry_game()
  if multiplayer_active() then
    state.menu_open = false
    state.game_started = true
    state.game_over = false
    state.game_over_pending = false
    state.score_reset_requested = true
    state.camera_reset_requested = true
    Runtime.set_physics_enabled(true)
    set_game_hud_visible(true)
    hide_menu()
    Entity.set_position(PLAYER_ID, state.respawn_x, state.respawn_y)
    Rigidbody2D.set_velocity(PLAYER_ID, 0.0, 0.0)
    return
  end

  state.auto_start = true
  state.game_over = false
  state.game_over_pending = false
  state.score_reset_requested = false
  state.extra_jumps = 0
  replication.reset_claims()
  Scene.load(active_scene())
end

-- @HandleAction("game_over_back")
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

-- @HandleAction("menu_button_levels")
function Menu.action_show_levels()
  show_levels()
end

-- @HandleAction("menu_button_network")
function Menu.action_show_network()
  show_network()
end

-- @HandleAction("menu_button_options")
function Menu.action_show_options()
  show_options()
end

-- @HandleAction("menu_button_level_1")
function Menu.action_level_1()
  select_level(SCENE_PLATFORMER, 1)
end

-- @HandleAction("menu_button_level_2")
function Menu.action_level_2()
  select_level(SCENE_SPIRAL, 2)
end

-- @HandleAction("menu_levels_back")
-- @HandleAction("menu_back")
function Menu.action_show_main()
  show_main()
end

-- @HandleAction("menu_network_back")
function Menu.action_network_back()
  if Menu.network_join_pending then
    replication.disconnect()
    Menu.network_join_pending = false
  end
  show_main()
end

-- @HandleAction("menu_button_quit")
function Menu.action_quit()
  Runtime.quit()
end

-- @HandleAction("menu_tab_sound")
function Menu.action_sound_tab()
  show_tab("sound")
end

-- @HandleAction("menu_tab_video")
function Menu.action_video_tab()
  show_tab("video")
end

-- @HandleAction("menu_volume_minus")
function Menu.action_volume_down()
  set_volume(Menu.volume - 0.10)
end

-- @HandleAction("menu_volume_plus")
function Menu.action_volume_up()
  set_volume(Menu.volume + 0.10)
end

-- @HandleAction("menu_window_dropdown")
function Menu.action_window_dropdown()
  cancel_max_fps_edit()
  Menu.dropdown_open = not Menu.dropdown_open
  update_video_hud()
  show_group("menu_dropdown", Menu.dropdown_open)
end

-- @HandleAction("menu_window_mode_windowed")
function Menu.action_windowed()
  set_window_mode("windowed")
end

-- @HandleAction("menu_window_mode_borderless")
function Menu.action_borderless()
  set_window_mode("borderless")
end

-- @HandleAction("menu_window_mode_fullscreen")
function Menu.action_fullscreen()
  set_window_mode("fullscreen")
end

-- @HandleAction("menu_max_fps_minus")
function Menu.action_fps_down()
  cancel_max_fps_edit()
  change_max_fps(-15)
end

-- @HandleAction("menu_max_fps_plus")
function Menu.action_fps_up()
  cancel_max_fps_edit()
  change_max_fps(15)
end

-- @HandleAction("menu_max_fps_input")
function Menu.action_fps_input()
  begin_max_fps_edit()
end

local function update_network_connection(dt)
  if not Menu.network_join_pending then
    return
  end

  Menu.network_join_elapsed = Menu.network_join_elapsed + dt
  local events = replication.process_events()
  local session = events.session or replication.current_session()
  if session ~= nil and session.scene_id ~= nil then
    Menu.network_join_pending = false
    update_network_hud("CONNECTED LEVEL " .. tostring(session.level or 1))
    select_level(session.scene_id, session.level or 1)
    return
  end

  if events.connected or replication.is_connected() then
    update_network_hud("CONNECTED - WAITING FOR SESSION")
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

  if pressed_once("f1") then
    Menu.network_level = 1
    update_network_hud(nil)
  elseif pressed_once("f2") then
    Menu.network_level = 2
    update_network_hud(nil)
  end
end

local function update_max_fps_input()
  if not Menu.max_fps_editing then
    return false
  end

  for digit = 0, 9 do
    local key = tostring(digit)
    if pressed_once(key) and string.len(Menu.max_fps_buffer) < 4 then
      Menu.max_fps_buffer = Menu.max_fps_buffer .. key
      update_video_hud()
    end
  end

  if pressed_once("backspace") and string.len(Menu.max_fps_buffer) > 0 then
    Menu.max_fps_buffer = string.sub(Menu.max_fps_buffer, 1, string.len(Menu.max_fps_buffer) - 1)
    update_video_hud()
  end

  if pressed_once("delete") then
    Menu.max_fps_buffer = ""
    update_video_hud()
  end

  if pressed_once("return") or pressed_once("space") then
    commit_max_fps_edit()
  elseif pressed_once("escape") then
    cancel_max_fps_edit()
  end
  return true
end

function Menu.update(dt)
  local enter_down = (Input.is_down("return") or Input.is_down("space")) and not Menu.max_fps_editing
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

  if Menu.screen == "options" and Menu.active_tab == "video" and not Menu.dropdown_open then
    if update_max_fps_input() then
      Menu.enter_was_down = enter_down
      return
    elseif Input.is_down("left") then
      change_max_fps(-1)
    elseif Input.is_down("right") then
      change_max_fps(1)
    end
  end

  Menu.enter_was_down = enter_down
end

return Menu
