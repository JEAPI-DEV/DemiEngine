# language_file

Loads YAML language assets through Demi's normal asset residency service and
applies their `variables` map to native HUD `${variable}` placeholders.

```lua
local LanguageFile = require("language_file")

local language = LanguageFile.new({
  fallback = "en",
  languages = {
    en = "asset://language/en",
    de = "asset://language/de",
  },
})

function Game:on_start()
  language:use("de") -- immediate when preloaded; otherwise starts a load
end

function Game:on_update()
  language:update() -- finishes any lazy asynchronous load
end
```

Language files use ordinary YAML and must contain a string mapping:

```yaml
locale: en
variables:
  game_title: Last Keep
  start_wave: "Start wave"
  multiline_hint: |
    Build towers.
    Protect the keep.
```

The package caches parsed language tables by stable asset ID. `clear()` drops a
parsed cache entry when an application deliberately wants to consume a reloaded
asset. YAML parsing itself is the generic `Data.parse_yaml` engine facility;
this package contains only language selection, fallback, and HUD application
policy.
