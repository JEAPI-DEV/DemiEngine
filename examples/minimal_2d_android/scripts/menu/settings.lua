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

function Settings.update_volume_hud()
  local menu = Settings.menu
  local fill_width = 308.0 * menu.volume
  Hud.set_rect("menu_volume_fill", 326.0, 305.0, fill_width, 16.0)
  Hud.set_rect("menu_volume_knob", 326.0 + fill_width - 4.0, 299.0, 8.0, 28.0)
  Hud.set_text("menu_volume_value", volume_label(menu))
end

function Settings.apply()
  local menu = Settings.menu
  menu.volume = Save.get_number(SETTINGS_SLOT, "master_volume", Audio.get_master_volume())
  Audio.set_master_volume(menu.volume)
  Runtime.set_max_fps(60)
end

function Settings.set_volume(volume)
  local menu = Settings.menu
  menu.volume = math.max(0.0, math.min(1.0, volume))
  Audio.set_master_volume(menu.volume)
  Save.set_number(SETTINGS_SLOT, "master_volume", menu.volume)
  Settings.update_volume_hud()
end

return Settings
