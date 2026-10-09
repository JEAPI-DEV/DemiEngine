local Templates = require("demi.ui.orbit.templates")
local Orbit = {}

local function copy(value)
  if type(value) ~= "table" then return value end
  local result = {}
  for key, item in pairs(value) do result[key] = copy(item) end
  return result
end

local defaults = {}
for key, parameter in pairs(Templates.showcase.parameters) do
  if type(parameter.default) == "string" and parameter.default:sub(1, 1) == "#" then
    defaults[key] = parameter.default
  end
end
local variants = {
  ember = {},
  glacier = { accent = "#7DCBFF", accent_hover = "#B4E3FF", positive = "#8ADABD" },
  meadow = { accent = "#B6DD83", accent_hover = "#D5EFB2", positive = "#7FD7C8" },
}

---Returns an independent palette. All overrides are #RRGGBB or #RRGGBBAA strings.
---@param name? string ember, glacier or meadow
---@param overrides? table
function Orbit.palette(name, overrides)
  name = name or "ember"
  assert(variants[name], "Unknown Orbit palette: " .. tostring(name))
  local result = copy(defaults)
  for key, value in pairs(variants[name]) do result[key] = value end
  for key, value in pairs(overrides or {}) do
    assert(defaults[key], "Unknown Orbit color: " .. tostring(key))
    assert(type(value) == "string" and (value:match("^#%x%x%x%x%x%x$") or value:match("^#%x%x%x%x%x%x%x%x$")),
      "Orbit colors must use #RRGGBB or #RRGGBBAA")
    result[key] = value
  end
  return result
end

local function values_for(kind, arguments, palette)
  local template = assert(Templates[kind], "Unknown Orbit component: " .. tostring(kind))
  local values = {}
  for key, parameter in pairs(template.parameters) do
    values[key] = copy(palette and palette[key] or parameter.default)
  end
  for key, value in pairs(arguments or {}) do
    local parameter = assert(template.parameters[key], "Unknown Orbit argument: " .. tostring(key))
    local expected = parameter.type
    assert(type(value) == expected or ((expected == "array" or expected == "object") and type(value) == "table"),
      "Wrong type for Orbit argument: " .. key)
    values[key] = copy(value)
  end
  return values
end

---Build a HUD prefab definition for either authored JSON or Hud.create.
---The engine expands the same prefab source in both cases.
function Orbit.node(kind, id, arguments, palette)
  assert(type(id) == "string" and id ~= "", "Orbit instances need a stable nonempty ID")
  values_for(kind, arguments, palette)
  local values = copy(arguments or {})
  for key, value in pairs(palette or {}) do
    if Templates[kind].parameters[key] and values[key] == nil then values[key] = value end
  end
  return { id = id, prefab = "ui-prefab://orbit/" .. kind, arguments = values }
end

function Orbit.button(id, label, options)
  options = copy(options or {})
  local variant = options.variant or "primary"
  local palette = options.palette
  options.variant, options.palette = nil, nil
  options.label = label
  return Orbit.node("button-" .. variant, id, options, palette)
end

---Stable asset URI for a bundled icon; use any asset URI for custom artwork.
function Orbit.icon_asset(name)
  assert(Templates._icons[name], "Unknown Orbit icon: " .. tostring(name))
  return "asset://orbit/icons/" .. name
end

function Orbit.icon(id, name, options)
  options = copy(options or {})
  options.texture = Orbit.icon_asset(name)
  return Orbit.node("icon", id, options)
end

function Orbit.icon_button(id, name, label, options)
  options = copy(options or {})
  local palette = options.palette
  options.palette = nil
  options.texture, options.label = Orbit.icon_asset(name), label
  return Orbit.node("icon-button", id, options, palette)
end

local Controls = require("demi.ui.orbit.controls")
Orbit.bind_tabs = Controls.tabs
Orbit.bind_dropdown = Controls.dropdown
Orbit.bind_choices = Controls.choices
Orbit.bind_stepper = Controls.stepper
Orbit.bind_pagination = Controls.pagination
Orbit.bind_tooltip = Controls.tooltip
Orbit.bind_dismiss = Controls.dismiss
Orbit.bind_dialog = Controls.dialog
return Orbit
