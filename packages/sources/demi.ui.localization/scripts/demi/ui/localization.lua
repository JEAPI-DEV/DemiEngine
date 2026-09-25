local Localization = {}
Localization.__index = Localization

local parsed_cache = {}

local function sorted_keys(values)
  local keys = {}
  for key in pairs(values) do keys[#keys + 1] = key end
  table.sort(keys)
  return keys
end

local function validate(document, asset_id)
  if type(document) ~= "table" or type(document.variables) ~= "table" then
    return nil, asset_id .. " requires a YAML 'variables' mapping"
  end
  local variables = {}
  for key, value in pairs(document.variables) do
    if type(key) ~= "string" or type(value) ~= "string" then
      return nil, asset_id .. " language variables must be strings"
    end
    variables[key] = value
  end
  return { locale = document.locale, variables = variables }
end

function Localization.new(options)
  options = options or {}
  local self = setmetatable({}, Localization)
  self.assets = options.assets or require("demi.assets")
  self.data = options.data or require("demi.data")
  self.hud = options.hud or require("demi.hud")
  self.languages = options.languages or {}
  self.fallback = options.fallback
  self.pending = {}
  self.requested = nil
  self.active = nil
  self.last_error = nil
  return self
end

function Localization:define(locale, asset_id)
  assert(type(locale) == "string" and locale ~= "")
  assert(type(asset_id) == "string" and asset_id:match("^asset://"))
  self.languages[locale] = asset_id
  return self
end

function Localization:_read(locale)
  local asset_id = self.languages[locale]
  if not asset_id then return false, "Unknown language: " .. tostring(locale) end
  if parsed_cache[asset_id] then return true end
  local text, text_error = self.assets.text(asset_id)
  if text then
    local document, parse_error = self.data.parse_yaml(text, asset_id)
    if not document then
      return false, parse_error and parse_error.message or "Invalid language YAML"
    end
    local parsed, validation_error = validate(document, asset_id)
    if not parsed then return false, validation_error end
    parsed_cache[asset_id] = parsed
    self.pending[locale] = nil
    return true
  end
  if self.pending[locale] then return false, "loading" end
  local request, load_error = self.assets.load(asset_id)
  if not request or request == 0 then
    return false, load_error ~= "" and load_error or text_error or "Language load failed"
  end
  self.pending[locale] = request
  return false, "loading"
end

function Localization:_ready(locale)
  return locale == nil or self:_read(locale)
end

function Localization:_apply(locale)
  local selected_id = self.languages[locale]
  local selected = selected_id and parsed_cache[selected_id]
  if not selected then return false, "Language is not loaded: " .. locale end
  local merged = {}
  if self.fallback and self.languages[self.fallback] then
    local fallback = parsed_cache[self.languages[self.fallback]]
    if fallback then
      for key, value in pairs(fallback.variables) do merged[key] = value end
    end
  end
  for key, value in pairs(selected.variables) do merged[key] = value end
  local applicable = {}
  for _, key in ipairs(sorted_keys(merged)) do
    -- A shared language file may contain variables for another HUD. Native HUD
    -- declarations remain the authority, so undeclared values are ignored.
    if self.hud.has_variable(key) then applicable[key] = merged[key] end
  end
  local changed, error = self.hud.set_variables(applicable)
  if not changed then return false, error end
  self.active = locale
  self.last_error = nil
  return true
end

function Localization:use(locale)
  self.requested = locale
  local fallback_ready, fallback_error = self:_ready(self.fallback)
  if not fallback_ready and fallback_error ~= "loading" then
    self.last_error = fallback_error
    return false, fallback_error
  end
  local ready, load_error = self:_read(locale)
  if not ready then
    self.last_error = load_error ~= "loading" and load_error or nil
    return false, load_error
  end
  if not fallback_ready then return false, "loading" end
  return self:_apply(locale)
end

function Localization:update()
  for locale, request in pairs(self.pending) do
    local progress = self.assets.progress(request)
    if progress.stage == "failed" or progress.stage == "cancelled" then
      self.pending[locale] = nil
      self.last_error = progress.error ~= "" and progress.error or
        ("Language load failed: " .. locale)
      return false, self.last_error
    end
    if progress.stage == "ready" then
      local ready, read_error = self:_read(locale)
      if not ready and read_error ~= "loading" then
        self.last_error = read_error
        return false, read_error
      end
    end
  end
  if self.requested and self.active == self.requested and not next(self.pending) then
    return true
  end
  if self.requested and not next(self.pending) then
    return self:_apply(self.requested)
  end
  return false, "loading"
end

function Localization:get(locale, name)
  local asset_id = self.languages[locale]
  local language = asset_id and parsed_cache[asset_id]
  return language and language.variables[name] or nil
end

function Localization:clear(locale)
  if locale then
    local asset_id = self.languages[locale]
    if asset_id then parsed_cache[asset_id] = nil end
  else
    parsed_cache = {}
  end
end

return Localization
