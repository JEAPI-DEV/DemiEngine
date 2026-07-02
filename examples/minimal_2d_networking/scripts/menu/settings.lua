local Settings = {}

local SETTINGS_SLOT = "settings"

function Settings.bind(menu, view)
  Settings.menu = menu
  Settings.view = view
  return Settings
end

local function volume_label(menu)
  return tostring(math.floor(menu.volume * 100.0 + 0.5)) .. " PCT"
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

local function max_fps_label(menu)
  if menu.max_fps_editing then
    return menu.max_fps_buffer == "" and "_" or menu.max_fps_buffer .. "_"
  end
  if menu.max_fps <= 0 then
    return "UNLIMITED"
  end
  return tostring(menu.max_fps)
end

function Settings.update_volume_hud()
  local menu = Settings.menu
  local fill_width = 308.0 * menu.volume
  Hud.set_rect("menu_volume_fill", 326.0, 305.0, fill_width, 16.0)
  Hud.set_rect("menu_volume_knob", 326.0 + fill_width - 4.0, 299.0, 8.0, 28.0)
  Hud.set_text("menu_volume_value", volume_label(menu))
end

function Settings.update_video_hud()
  local menu = Settings.menu
  Hud.set_text("menu_max_fps_label", "Max-FPS:")
  Hud.set_text("menu_window_mode_label", mode_label(menu.window_mode))
  Hud.set_text("menu_dropdown_arrow", menu.dropdown_open and "-" or "+")
  Hud.set_text("menu_max_fps_value", max_fps_label(menu))
  Hud.set_color("menu_max_fps_input", menu.max_fps_editing and 0.12 or 0.05, menu.max_fps_editing and 0.16 or 0.06, menu.max_fps_editing and 0.28 or 0.13, 0.94)
  Hud.set_visible("menu_back", not menu.dropdown_open)
end

function Settings.apply()
  local menu = Settings.menu
  menu.volume = Save.get_number(SETTINGS_SLOT, "master_volume", Audio.get_master_volume())
  menu.window_mode = Save.get_string(SETTINGS_SLOT, "window_mode", Runtime.get_window_mode())
  menu.max_fps = Save.get_number(SETTINGS_SLOT, "max_fps", Runtime.get_max_fps())
  Audio.set_master_volume(menu.volume)
  Runtime.set_window_mode(menu.window_mode)
  Runtime.set_max_fps(menu.max_fps)
end

function Settings.set_volume(volume)
  local menu = Settings.menu
  menu.volume = math.max(0.0, math.min(1.0, volume))
  Audio.set_master_volume(menu.volume)
  Save.set_number(SETTINGS_SLOT, "master_volume", menu.volume)
  Settings.update_volume_hud()
end

function Settings.set_window_mode(mode)
  local menu = Settings.menu
  menu.window_mode = mode
  Runtime.set_window_mode(mode)
  Save.set_string(SETTINGS_SLOT, "window_mode", mode)
  menu.dropdown_open = false
  Settings.update_video_hud()
  Settings.view.show_group("menu_dropdown", false)
end

function Settings.set_max_fps(value)
  local menu = Settings.menu
  if value <= 0 then
    menu.max_fps = 0
  else
    menu.max_fps = math.max(15, math.min(1000, math.floor(value + 0.5)))
  end
  Runtime.set_max_fps(menu.max_fps)
  Save.set_number(SETTINGS_SLOT, "max_fps", menu.max_fps)
  Settings.update_video_hud()
end

function Settings.begin_max_fps_edit()
  local menu = Settings.menu
  menu.max_fps_editing = true
  menu.max_fps_buffer = menu.max_fps <= 0 and "" or tostring(menu.max_fps)
  Settings.update_video_hud()
end

function Settings.commit_max_fps_edit()
  local menu = Settings.menu
  if not menu.max_fps_editing then
    return
  end
  local value = tonumber(menu.max_fps_buffer)
  menu.max_fps_editing = false
  menu.max_fps_buffer = ""
  Settings.set_max_fps(value or 0)
end

function Settings.cancel_max_fps_edit()
  local menu = Settings.menu
  menu.max_fps_editing = false
  menu.max_fps_buffer = ""
  Settings.update_video_hud()
end

function Settings.change_max_fps(delta)
  local menu = Settings.menu
  if menu.max_fps <= 0 and delta > 0 then
    Settings.set_max_fps(60)
    return
  end
  if menu.max_fps <= 15 and delta < 0 then
    Settings.set_max_fps(0)
    return
  end
  Settings.set_max_fps(menu.max_fps + delta)
end

function Settings.update_max_fps_input(pressed_once)
  local menu = Settings.menu
  if not menu.max_fps_editing then
    return false
  end

  for digit = 0, 9 do
    local key = tostring(digit)
    if pressed_once(key) and string.len(menu.max_fps_buffer) < 4 then
      menu.max_fps_buffer = menu.max_fps_buffer .. key
      Settings.update_video_hud()
    end
  end

  if pressed_once("backspace") and string.len(menu.max_fps_buffer) > 0 then
    menu.max_fps_buffer = string.sub(menu.max_fps_buffer, 1, string.len(menu.max_fps_buffer) - 1)
    Settings.update_video_hud()
  end

  if pressed_once("delete") then
    menu.max_fps_buffer = ""
    Settings.update_video_hud()
  end

  if pressed_once("return") or pressed_once("space") then
    Settings.commit_max_fps_edit()
  elseif pressed_once("escape") then
    Settings.cancel_max_fps_edit()
  end
  return true
end

return Settings
