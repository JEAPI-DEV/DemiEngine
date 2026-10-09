-- Compound-control policy stays in the package; the engine owns input/layout.
local Controls = {}
local function bind(callback)
  local Events = require("demi.events")
  local state = {}
  local subscription = Events.subscribe("ui_event", callback)
  function state:dispose()
    if subscription then Events.unsubscribe(subscription); subscription = nil end
  end
  return state
end
local function pressed(event) return event.type == "submit" end
local function changed(options, ...) if options and options.on_change then options.on_change(...) end end
local function require_node(Hud, id) assert(Hud.find(id), "Missing Orbit control: " .. id) end

function Controls.tabs(root, options)
  local Hud = require("demi.hud")
  options = options or {}
  local state
  state = bind(function(e)
    if not pressed(e) then return end
    for i = 1, 3 do if e.id == root .. ".tab" .. i then state:select(i) end end
  end)
  function state:select(index)
    assert(index >= 1 and index <= 3 and index % 1 == 0, "Tab index must be 1..3")
    for i = 1, 3 do
      Hud.set_visible(root .. ".indicator" .. i, i == index)
      if options.panels and options.panels[i] then Hud.set_visible(options.panels[i], i == index) end
    end
    self.value = index
    changed(options, index)
  end
  require_node(Hud, root .. ".tab1")
  state:select(options.selected or 1)
  return state
end

function Controls.dropdown(root, options)
  local Hud = require("demi.hud")
  options = options or {}
  require_node(Hud, root .. ".trigger")
  local backdrop_id = root .. ".backdrop"
  assert(not Hud.find(backdrop_id), "Dropdown is already bound: " .. root)
  local backdrop, error = Hud.create("", {id=backdrop_id, type="button", dock="fill",
    visible=false, focusable=false, accessibility_hidden=true, background_color="#00000000", hover_color="#00000000", layer=199})
  assert(backdrop, error)
  local state
  state = bind(function(e)
    if e.type == "cancel" then state:open(false); return end
    if not pressed(e) then return end
    if e.id == root .. ".trigger" then state:open(not state.opened)
    elseif e.id == backdrop_id then state:open(false)
    else
      for i = 1, 3 do if e.id == root .. ".option" .. i then state:select(i); return end end
      if state.opened then state:open(false) end
    end
  end)
  function state:open(value)
    self.opened = value
    Hud.set_visible(backdrop_id, value)
    Hud.set_visible(root .. ".menu", value)
  end
  function state:select(index)
    assert(index >= 1 and index <= 3 and index % 1 == 0, "Dropdown index must be 1..3")
    local text = assert(Hud.get_text(root .. ".option" .. index))
    self.value = index
    Hud.set_text(root .. ".trigger", text)
    self:open(false)
    changed(options, index, text)
  end
  local unsubscribe = state.dispose
  function state:dispose() unsubscribe(self); if Hud.find(backdrop_id) then Hud.remove(backdrop) end end
  state:select(options.selected or 2)
  return state
end

function Controls.choices(root, options)
  local Hud = require("demi.hud")
  options = options or {}
  require_node(Hud, root .. ".option1")
  local state
  state = bind(function(e)
    if not pressed(e) then return end
    for i = 1, 3 do if e.id == root .. ".option" .. i then state:select(i) end end
  end)
  function state:select(index)
    assert(index >= 1 and index <= 3 and index % 1 == 0, "Choice index must be 1..3")
    self.value = index
    for i = 1, 3 do Hud.set_checked(root .. ".option" .. i, i == index) end
    changed(options, index)
  end
  state:select(options.selected or 1)
  return state
end

function Controls.stepper(root, options)
  local Hud = require("demi.hud")
  options = options or {}
  local minimum, maximum, step = options.minimum or 0, options.maximum or 99, options.step or 1
  assert(minimum <= maximum and step > 0, "Invalid stepper bounds")
  require_node(Hud, root .. ".value")
  local state
  state = bind(function(e)
    if not pressed(e) then return end
    if e.id == root .. ".minus" then state:set(state.value - step)
    elseif e.id == root .. ".plus" then state:set(state.value + step) end
  end)
  function state:set(value)
    self.value = math.max(minimum, math.min(maximum, value))
    Hud.set_text(root .. ".value", tostring(self.value))
    Hud.set_disabled(root .. ".minus", self.value <= minimum)
    Hud.set_disabled(root .. ".plus", self.value >= maximum)
    changed(options, self.value)
  end
  state:set(options.value or tonumber(Hud.get_text(root .. ".value")) or minimum)
  return state
end

function Controls.pagination(root, options)
  local Hud = require("demi.hud")
  options = options or {}
  local count = options.pages or 3
  assert(count >= 1 and count % 1 == 0, "Page count must be a positive integer")
  require_node(Hud, root .. ".page1")
  local state
  state = bind(function(e)
    if not pressed(e) then return end
    if e.id == root .. ".previous" then state:select(state.value - 1)
    elseif e.id == root .. ".next" then state:select(state.value + 1)
    else for i = 1, 3 do if e.id == root .. ".page" .. i then state:select(state.first + i - 1) end end end
  end)
  function state:select(page)
    self.value = math.max(1, math.min(count, page))
    self.first = math.max(1, math.min(self.value - 1, math.max(1, count - 2)))
    for i = 1, 3 do
      local number = self.first + i - 1
      Hud.set_visible(root .. ".page" .. i, number <= count)
      Hud.set_text(root .. ".page" .. i, (number == self.value and "[" or "") .. number .. (number == self.value and "]" or ""))
    end
    Hud.set_disabled(root .. ".previous", self.value == 1)
    Hud.set_disabled(root .. ".next", self.value == count)
    changed(options, self.value)
  end
  state:select(options.selected or 1)
  return state
end

function Controls.tooltip(trigger, tooltip)
  local Hud = require("demi.hud")
  require_node(Hud, trigger); require_node(Hud, tooltip)
  Hud.set_visible(tooltip, false)
  return bind(function(e)
    if e.id ~= trigger then return end
    if e.type == "pointer_enter" or e.type == "focus_gained" then Hud.set_visible(tooltip, true)
    elseif e.type == "pointer_exit" or e.type == "focus_lost" or e.type == "cancel" then Hud.set_visible(tooltip, false) end
  end)
end

function Controls.dismiss(root, options)
  local Hud = require("demi.hud")
  options = options or {}
  require_node(Hud, root .. ".close")
  return bind(function(e)
    if pressed(e) and e.id == root .. ".close" then Hud.set_visible(root, false); changed(options, false) end
  end)
end

function Controls.dialog(root, options)
  local Hud = require("demi.hud")
  options = options or {}
  require_node(Hud, root)
  local state
  state = bind(function(e)
    if not pressed(e) and e.type ~= "cancel" then return end
    if e.id == root .. ".confirm" then state:close(true)
    elseif e.id == root .. ".cancel" or (state.opened and e.type == "cancel") then state:close(false) end
  end)
  function state:open() self.opened = true; Hud.set_visible(root, true); Hud.focus_next() end
  function state:close(confirmed) self.opened = false; Hud.set_visible(root, false); changed(options, confirmed) end
  Hud.set_visible(root, false)
  return state
end
return Controls
