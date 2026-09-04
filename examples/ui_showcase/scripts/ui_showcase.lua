-- UI showcase: searchable virtualized list, scroll panel, dropdown, tabs.
-- Uses require("demi.ui") helpers over the raw Hud/Events API: no per-frame
-- polling, no manual scroll clamp math, no subscription bookkeeping.
local Ui = require("demi.ui")
local Script = require("demi.script")
local UiShowcase = {}

local inventory_items = {}
for index = 1, 10000 do
  inventory_items[index] = {
    id = "item_" .. index,
    label = (index % 3 == 1 and "Potion " or index % 3 == 2 and "Key " or "Sword ") .. index,
  }
end

local window_modes = {
  window_mode_windowed = { mode = "windowed", label = "Windowed" },
  window_mode_borderless = { mode = "borderless", label = "Borderless" },
  window_mode_fullscreen = { mode = "fullscreen", label = "Fullscreen" },
}

local ROW_EXTENT = 38
local VIEWPORT = 300

local function refresh(pattern)
  Ui.filter_list("inventory_rows", "inventory_row_template", inventory_items,
    pattern or "",
    function(item) return item.id end,
    function(item, wanted)
      return wanted == "" or (Regex.is_valid(wanted) and Regex.matches(item.label, wanted))
    end,
    function(row, item) Hud.set_text(row.node.id, item.label) end,
    ROW_EXTENT, VIEWPORT)
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

function UiShowcase:on_start()
  Script.bind(self)
  refresh("")
  local choice = current_window_mode()
  Ui.dropdown("window_mode_dropdown", "window_mode_options", false, "Window: " .. choice.label .. "  v")
  -- Poll-free search: refresh only when the text input changes.
  self:on("ui_value_changed", function(event)
    if event.id == "inventory_search" then
      refresh(event.text)
    end
  end)
  -- Scroll panel owns clamp math + ui_scroll subscription.
  self.scroll = Ui.scroll_panel("inventory_panel", ROW_EXTENT, VIEWPORT,
    function()
      return #inventory_items
    end,
    function(_scroll)
      refresh("")
    end)
end

function UiShowcase:on_destroy()
  Script.release(self)
  if self.scroll then
    self.scroll:release()
  end
  Hud.clear_recycled_rows("inventory_rows")
end

-- Re-export Script helper methods so self:on/self:after work.
for key, value in pairs(Script) do
  if UiShowcase[key] == nil then
    UiShowcase[key] = value
  end
end

-- @HandleAction("window_mode_dropdown")
-- @HandleAction("window_mode_windowed")
-- @HandleAction("window_mode_borderless")
-- @HandleAction("window_mode_fullscreen")
function UiShowcase:on_window_mode_action(event)
  if event.action == "window_mode_dropdown" then
    local open = not UiShowcase.dropdown_open
    UiShowcase.dropdown_open = open
    Ui.dropdown("window_mode_dropdown", "window_mode_options", open,
      "Window: " .. current_window_mode().label .. (open and "  ^" or "  v"))
    return
  end

  local choice = window_modes[event.action]
  if choice then
    UiShowcase.dropdown_open = false
    Ui.dropdown("window_mode_dropdown", "window_mode_options", false,
      "Window: " .. choice.label .. "  v")
    Application.set_window_mode(choice.mode)
  end
end

return UiShowcase
