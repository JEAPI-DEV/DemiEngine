# Main Menu Languages

Demonstrates native HUD `${variable}` text, the `language_file` package,
preloaded English YAML, and lazily loaded German YAML.

- Press `1` for English.
- Press `2` for German.
- English is listed in project `assets` and is resident at startup.
- German is loaded only on first use, then its resident text and parsed language
  table remain cached.
