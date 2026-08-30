local LanguageFile = require("language_file")

local texts = {
  ["asset://language/en"] = "en",
  ["asset://language/de"] = "de",
}
local documents = {
  en = { locale = "en", variables = { title = "Keep", play = "Play" } },
  de = { locale = "de", variables = { title = "Burg" } },
}
local applied = {}
local resets = 0
local fake_assets = {
  text = function(id) return texts[id], texts[id] and "" or "missing" end,
  load = function() return 1, "" end,
  progress = function() return { stage = "ready", error = "" } end,
}
local fake_data = {
  parse_yaml = function(text) return documents[text], nil end,
}
local fake_hud = {
  has_variable = function() return true end,
  set_variables = function(values)
    resets = resets + 1
    applied = values
    return true, ""
  end,
}

Test.case("selected language overrides fallback variables", function()
  local languages = LanguageFile.new({
    assets = fake_assets, data = fake_data, hud = fake_hud,
    fallback = "en",
    languages = { en = "asset://language/en", de = "asset://language/de" },
  })
  local ready, error = languages:use("de")
  Test.equal(ready, true); Test.equal(error, nil)
  Test.equal(applied.title, "Burg"); Test.equal(applied.play, "Play")
  Test.equal(languages:get("de", "title"), "Burg")
  Test.equal(resets, 1)
end)

Test.case("loaded documents are parsed once and cached", function()
  local parses = 0
  local data = { parse_yaml = function(text)
    parses = parses + 1; return documents[text], nil
  end }
  local languages = LanguageFile.new({
    assets = fake_assets, data = data, hud = fake_hud,
    languages = { en = "asset://language/en" },
  })
  languages:clear("en")
  Test.equal(languages:use("en"), true)
  Test.equal(languages:use("en"), true)
  Test.equal(parses, 1)
end)
