local UiShowcase = {}

local inventory_items = {
  { id = "slot_1", label = "Potion" },
  { id = "slot_2", label = "Key" },
  { id = "slot_3", label = "Sword" },
}

local previous_pattern = nil

local function filter_inventory(pattern)
  local valid = Regex.is_valid(pattern)
  for _, item in ipairs(inventory_items) do
    local visible = pattern == "" or (valid and Regex.matches(item.label, pattern))
    Hud.set_visible(item.id, visible)
  end
end

function UiShowcase:on_start()
  previous_pattern = ""
  filter_inventory("")
end

function UiShowcase:on_update(_dt)
  local pattern = Hud.get_text("inventory_search") or ""
  if pattern == previous_pattern then
    return
  end

  previous_pattern = pattern
  filter_inventory(pattern)
end

return UiShowcase
