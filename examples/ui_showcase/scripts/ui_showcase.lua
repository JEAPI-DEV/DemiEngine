local UiShowcase = {}

local inventory_items = {}
local inventory_by_id = {}
for index = 1, 10000 do
  inventory_items[index] = {
    id = "item_" .. index,
    label = (index % 3 == 1 and "Potion " or index % 3 == 2 and "Key " or "Sword ") .. index,
  }
  inventory_by_id[inventory_items[index].id] = inventory_items[index]
end

local previous_pattern = nil
local window_mode_dropdown_open = false
local inventory_scroll = 0
local inventory_scroll_subscription = nil
local filtered_inventory_keys = {}
local filtered_inventory_extents = {}

local window_modes = {
  window_mode_windowed = { mode = "windowed", label = "Windowed" },
  window_mode_borderless = { mode = "borderless", label = "Borderless" },
  window_mode_fullscreen = { mode = "fullscreen", label = "Fullscreen" },
}

local function show_window_mode(mode, label)
  Hud.set_text("window_mode_dropdown", "Window: " .. label .. "  v")
  Hud.set_visible("window_mode_options", false)
  window_mode_dropdown_open = false
  if mode then
    Application.set_window_mode(mode)
  end
end

local function current_window_mode()
  local mode = Application.window_mode()
  for _, choice in pairs(window_modes) do
    if choice.mode == mode then
      return choice
    end
  end
  return window_modes.window_mode_windowed
end

local function filter_inventory(pattern)
  local valid = Regex.is_valid(pattern)
  local keys = {}
  local extents = {}
  for _, item in ipairs(inventory_items) do
    if pattern == "" or (valid and Regex.matches(item.label, pattern)) then
      keys[#keys + 1] = item.id
      extents[#extents + 1] = 38
    end
  end
  filtered_inventory_keys = keys
  filtered_inventory_extents = extents
  local maximum_scroll = math.max(#keys * 38 - 300, 0)
  inventory_scroll = math.max(0, math.min(inventory_scroll, maximum_scroll))
  local template = assert(Hud.find("inventory_row_template"))
  local rows, error = Hud.recycle_rows(
    "inventory_rows", template, keys, extents, inventory_scroll, 300, 2
  )
  assert(error == "", error)
  for _, row in ipairs(rows) do
    local item = inventory_by_id[row.key]
    Hud.set_text(row.node.id, item and item.label or row.key)
  end
end

function UiShowcase:on_start()
  previous_pattern = ""
  filter_inventory("")
  local choice = current_window_mode()
  show_window_mode(nil, choice.label)
  inventory_scroll_subscription = Events.subscribe("ui_scroll", function(event)
    if event.id ~= "inventory_panel" then
      return
    end
    local scale = event.source == "touch" and 1 or 38
    local maximum_scroll = math.max(#filtered_inventory_keys * 38 - 300, 0)
    inventory_scroll = math.max(
      0,
      math.min(inventory_scroll + event.delta_y * scale, maximum_scroll)
    )
    filter_inventory(previous_pattern or "")
  end)
end

function UiShowcase:on_destroy()
  if inventory_scroll_subscription then
    Events.unsubscribe(inventory_scroll_subscription)
    inventory_scroll_subscription = nil
  end
  Hud.clear_recycled_rows("inventory_rows")
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
    Hud.set_text(
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
