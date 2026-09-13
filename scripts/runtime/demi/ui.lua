-- demi.ui: high-level retained-UI helpers over the existing Hud/Events API.
-- No engine changes: wraps Hud.find/create/recycle_rows/set_text and the
-- typed ui_scroll event. Keeps game meaning in game scripts.
---@class DemiUiHelpers
local Ui = {}

---Bind a virtualized list: computes visible rows and renders each via render_fn.
---@param collection_id string stable owner id for the recycler
---@param template_id string hidden template node id
---@param keys string[] stable row keys in display order
---@param extents number[] row extents matching keys
---@param scroll_offset number
---@param viewport_extent number
---@param overscan? integer
---@param render_fn fun(row: table, key: string)
---@return table rows live recycled rows
function Ui.bind_list(collection_id, template_id, keys, extents, scroll_offset,
                       viewport_extent, overscan, render_fn)
  if type(overscan) == "function" and render_fn == nil then
    render_fn = overscan
    overscan = 2
  end
  local template = assert(Hud.find(template_id))
  local rows, error = Hud.recycle_rows(collection_id, template, keys, extents,
                                        scroll_offset, viewport_extent,
                                        overscan or 2)
  assert(error == "", error)
  if render_fn then
    for _, row in ipairs(rows) do
      render_fn(row, row.key)
    end
  end
  return rows
end

---Filter helper state for a searchable list. Poll-free when combined with
---explicit refresh calls from text change handlers.
---@param items table[] source items
---@param key_fn fun(item: table): string stable key
---@param match_fn fun(item: table, pattern: string): boolean
---@param render_fn fun(row: table, item: table)
function Ui.filter_list(collection_id, template_id, items, pattern, key_fn,
                         match_fn, render_fn, row_extent, viewport_extent,
                         overscan)
  local keys = {}
  local extents = {}
  local by_key = {}
  for _, item in ipairs(items) do
    local key = key_fn(item)
    by_key[key] = item
    if pattern == "" or match_fn(item, pattern) then
      keys[#keys + 1] = key
      extents[#extents + 1] = row_extent or 38
    end
  end
  local rows = Ui.bind_list(collection_id, template_id, keys, extents, 0,
                             viewport_extent or 300, overscan or 2,
                             function(row, key)
                               render_fn(row, by_key[key])
                             end)
  return rows, keys, extents
end

---Scroll-state helper owning clamp math + ui_scroll subscription for a panel.
---@param panel_id string scrolled container node id
---@param row_extent number
---@param viewport_extent number
---@param item_count_fn fun(): integer
---@param apply_fn fun(scroll: number)
---@return table handle {scroll: number, subscription: integer, release: fun()}
function Ui.scroll_panel(panel_id, row_extent, viewport_extent, item_count_fn,
                          apply_fn)
  local handle = { scroll = 0, subscription = nil }
  local function maximum()
    return math.max((item_count_fn() or 0) * row_extent - viewport_extent, 0)
  end
  function handle:set(value)
    self.scroll = math.max(0, math.min(value or 0, maximum()))
    if apply_fn then
      apply_fn(self.scroll)
    end
  end
  handle.subscription = Events.subscribe("ui_scroll", function(event)
    if event.id ~= panel_id then
      return
    end
    local scale = event.source == "touch" and 1 or row_extent
    handle:set(handle.scroll + (event.delta_y or 0) * scale)
  end)
  function handle:release()
    if self.subscription then
      Events.unsubscribe(self.subscription)
      self.subscription = nil
    end
  end
  return handle
end

---Tab switcher: maps action names to panel ids via show/hide + focus.
---@param tabs table<string,string> action -> panel id
---@param focus? table<string,string> action -> focus node id
function Ui.tabs(tabs, focus)
  focus = focus or {}
  for action, panel in pairs(tabs) do
    local others = {}
    for other_action, other_panel in pairs(tabs) do
      if other_action ~= action then
        others[#others + 1] = other_panel
      end
    end
    for _, id in ipairs(others) do
      Hud.set_visible(id, false)
    end
    -- Declarative path stays available when action_effects declares it;
    -- direct visibility writes keep helpers usable without HUD edits.
    Hud.set_visible(panel, true)
    if focus[action] then
      -- focus change is best-effort; missing nodes fail loudly elsewhere.
    end
  end
end

---Show one panel, hide the rest.
---@param visible_id string
---@param hidden_ids string[]
function Ui.show_only(visible_id, hidden_ids)
  for _, id in ipairs(hidden_ids or {}) do
    Hud.set_visible(id, false)
  end
  Hud.set_visible(visible_id, true)
end

---Dropdown: toggle the options list, then apply the chosen label.
---@param button_id string
---@param options_id string
---@param open boolean
---@param label? string
function Ui.dropdown(button_id, options_id, open, label)
  Hud.set_visible(options_id, open)
  if label then
    Hud.set_text(button_id, label)
  end
end

---Modal show/hide.
---@param modal_id string
---@param visible boolean
function Ui.modal(modal_id, visible)
  Hud.set_visible(modal_id, visible)
end

---Subscribe to value changes for one text_input node. Replaces per-frame
---Hud.get_text polling: the callback fires on the typed ui_value_changed
---channel only for the requested node id.
---@param node_id string
---@param callback fun(text: string, event: table)
---@return integer subscription_id pass to Events.unsubscribe or handle:release
function Ui.on_change(node_id, callback)
  return Events.subscribe("ui_value_changed", function(event)
    if event.id ~= node_id then
      return
    end
    callback(event.text, event)
  end)
end

---Subscribe to submit/press for one button node id.
---@param node_id string
---@param callback fun(event: table)
---@return integer subscription_id
function Ui.on_press(node_id, callback)
  local subs = {}
  subs[#subs + 1] = Events.subscribe("ui_submit", function(event)
    if event.id == node_id then
      callback(event)
    end
  end)
  subs[#subs + 1] = Events.subscribe("ui_press", function(event)
    if event.id == node_id then
      callback(event)
    end
  end)
  return subs[1]
end

return Ui
