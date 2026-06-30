local state = require("game_state")

local Menu = {
  screen = "main",
  active_tab = "sound",
  dropdown_open = false,
  volume = 1.0,
  window_mode = "windowed",
  enter_was_down = false,
}

local PLAYER_ID = "ent_player"
local SETTINGS_SLOT = "settings"
local SCENE_MAIN = "scene://minimal_2d/main"
local SCENE_SPIRAL = "scene://minimal_2d/spiral"

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
  show_group("menu_options", false)
  show_group("menu_sound", false)
  show_group("menu_video", false)
  show_group("menu_dropdown", false)
end

local function show_main()
  Menu.screen = "main"
  Menu.dropdown_open = false
  show_group("menu_base", true)
  show_group("menu_main", true)
  show_group("menu_levels", false)
  show_group("menu_options", false)
  show_group("menu_sound", false)
  show_group("menu_video", false)
  show_group("menu_dropdown", false)
end

local function show_levels()
  Menu.screen = "levels"
  Menu.dropdown_open = false
  show_group("menu_base", true)
  show_group("menu_main", false)
  show_group("menu_levels", true)
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
  Scene.load(scene_id)
end

function Menu.start_game()
  select_level(SCENE_MAIN, 1)
end

function Menu.apply_settings()
  Menu.volume = Save.get_number(SETTINGS_SLOT, "master_volume", Audio.get_master_volume())
  Menu.window_mode = Save.get_string(SETTINGS_SLOT, "window_mode", Runtime.get_window_mode())
  Audio.set_master_volume(Menu.volume)
  Runtime.set_window_mode(Menu.window_mode)
end

function Menu.start()
  state.menu_open = true
  Menu.screen = "main"
  Menu.active_tab = "sound"
  Menu.dropdown_open = false
  Menu.apply_settings()
  Runtime.set_physics_enabled(false)
  Hud.set_visible("points", false)
  show_main()
end

function Menu.begin_active_level()
  hide_menu()
  state.menu_open = false
  state.game_started = true
  Runtime.set_physics_enabled(true)
  Hud.set_visible("points", true)
  local px, py = Entity.get_position(PLAYER_ID)
  if px ~= nil and py ~= nil then
    state.respawn_x = px
    state.respawn_y = py
  end
  Entity.set_position(PLAYER_ID, state.respawn_x, state.respawn_y)
  Rigidbody2D.set_velocity(PLAYER_ID, 0.0, 0.0)
end

function Menu.handle_click(id)
  if id == "menu_button_levels" then
    show_levels()
  elseif id == "menu_button_level_1" then
    select_level(SCENE_MAIN, 1)
  elseif id == "menu_button_level_2" then
    select_level(SCENE_SPIRAL, 2)
  elseif id == "menu_levels_back" then
    show_main()
  elseif id == "menu_button_options" then
    show_options()
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
      select_level(SCENE_MAIN, 1)
    elseif Input.is_down("2") then
      select_level(SCENE_SPIRAL, 2)
    elseif Input.is_down("escape") then
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
