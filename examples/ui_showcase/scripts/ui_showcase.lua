local UiShowcase = {}

local inventory_items = {
  { id = "slot_1", label = "Potion" },
  { id = "slot_2", label = "Key" },
  { id = "slot_3", label = "Sword" },
}

local previous_pattern = nil
local window_mode_dropdown_open = false

local window_modes = {
  window_mode_windowed = { mode = "windowed", label = "Windowed" },
  window_mode_borderless = { mode = "borderless", label = "Borderless" },
  window_mode_fullscreen = { mode = "fullscreen", label = "Fullscreen" },
}

local function show_window_mode(mode, label)
  Hud.set_button_label("window_mode_dropdown", "Window: " .. label .. "  v")
  Hud.set_visible("window_mode_options", false)
  window_mode_dropdown_open = false
  if mode then
    Runtime.set_window_mode(mode)
  end
end

local function current_window_mode()
  local mode = Runtime.get_window_mode()
  for _, choice in pairs(window_modes) do
    if choice.mode == mode then
      return choice
    end
  end
  return window_modes.window_mode_windowed
end

local function filter_inventory(pattern)
  local valid = Regex.is_valid(pattern)
  for _, item in ipairs(inventory_items) do
    local visible = pattern == "" or (valid and Regex.matches(item.label, pattern))
    Hud.set_visible(item.id, visible)
  end
end

function UiShowcase:on_start()
  for _, item in ipairs(inventory_items) do
    local handle, error = Hud.create("inventory_grid", {
      id = item.id,
      type = "button",
      style = "control",
      text = item.label,
      action = "use:" .. item.id,
      width = 72,
      height = 72,
    })
    assert(handle, error)
  end
  previous_pattern = ""
  filter_inventory("")
  local choice = current_window_mode()
  show_window_mode(nil, choice.label)
end

function UiShowcase:on_update(_dt)
  local pattern = Hud.get_text("inventory_search") or ""
  if pattern == previous_pattern then
    return
  end

  previous_pattern = pattern
  filter_inventory(pattern)
end

-- @HandleAction("window_mode_dropdown")
-- @HandleAction("window_mode_windowed")
-- @HandleAction("window_mode_borderless")
-- @HandleAction("window_mode_fullscreen")
function UiShowcase:on_window_mode_action(event)
  if event.action == "window_mode_dropdown" then
    window_mode_dropdown_open = not window_mode_dropdown_open
    Hud.set_visible("window_mode_options", window_mode_dropdown_open)
    Hud.set_button_label(
      "window_mode_dropdown",
      "Window: " .. current_window_mode().label .. (window_mode_dropdown_open and "  ^" or "  v")
    )
    return
  end

  local choice = window_modes[event.action]
  if choice then
    show_window_mode(choice.mode, choice.label)
  end
end

return UiShowcase
