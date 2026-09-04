local LanguageFile = require("language_file")

local MainMenu = {}

function MainMenu:on_create()
  self.languages = LanguageFile.new({
    fallback = "en",
    languages = {
      en = "asset://language/en",
      de = "asset://language/de",
    },
  })
  self.pending_locale = nil
end

function MainMenu:request_language(locale)
  local ready, error = self.languages:use(locale)
  if ready then
    print("Language active: " .. locale .. "; play=" ..
      tostring(Hud.get_variable("play")))
    self.pending_locale = nil
  elseif error == "loading" then
    print("Language loading: " .. locale)
    self.pending_locale = locale
  else
    print("Language failed: " .. tostring(error))
  end
end

function MainMenu:on_start()
  Input.enable_context("menu")
  self:request_language("en")
end

function MainMenu:on_update()
  if Input.pressed("language_english") then
    self:request_language("en")
  elseif Input.pressed("language_german") then
    self:request_language("de")
  end
  if self.pending_locale then
    local ready, error = self.languages:update()
    if ready then
      print("Language active: " .. self.pending_locale .. "; play=" ..
        tostring(Hud.get_variable("play")))
      self.pending_locale = nil
    elseif error ~= "loading" then
      print("Language failed: " .. tostring(error))
      self.pending_locale = nil
    end
  end
end

return MainMenu
