-- Shared HUD prefab namespace. Game systems use local semantic IDs; native UI
-- identities remain explicit and stable across all floor HUD compositions.
local Native = require("demi.hud")
local Ui = {}
local scopes = {
  ["bag"] = "inventory",
  ["bag_capacity"] = "inventory",
  ["bag_close"] = "inventory",
  ["bag_frame"] = "inventory",
  ["bag_message"] = "inventory",
  ["bag_slot_1"] = "inventory",
  ["bag_slot_10"] = "inventory",
  ["bag_slot_10_count"] = "inventory",
  ["bag_slot_10_equipped"] = "inventory",
  ["bag_slot_10_icon"] = "inventory",
  ["bag_slot_11"] = "inventory",
  ["bag_slot_11_count"] = "inventory",
  ["bag_slot_11_equipped"] = "inventory",
  ["bag_slot_11_icon"] = "inventory",
  ["bag_slot_12"] = "inventory",
  ["bag_slot_12_count"] = "inventory",
  ["bag_slot_12_equipped"] = "inventory",
  ["bag_slot_12_icon"] = "inventory",
  ["bag_slot_1_count"] = "inventory",
  ["bag_slot_1_equipped"] = "inventory",
  ["bag_slot_1_icon"] = "inventory",
  ["bag_slot_2"] = "inventory",
  ["bag_slot_2_count"] = "inventory",
  ["bag_slot_2_equipped"] = "inventory",
  ["bag_slot_2_icon"] = "inventory",
  ["bag_slot_3"] = "inventory",
  ["bag_slot_3_count"] = "inventory",
  ["bag_slot_3_equipped"] = "inventory",
  ["bag_slot_3_icon"] = "inventory",
  ["bag_slot_4"] = "inventory",
  ["bag_slot_4_count"] = "inventory",
  ["bag_slot_4_equipped"] = "inventory",
  ["bag_slot_4_icon"] = "inventory",
  ["bag_slot_5"] = "inventory",
  ["bag_slot_5_count"] = "inventory",
  ["bag_slot_5_equipped"] = "inventory",
  ["bag_slot_5_icon"] = "inventory",
  ["bag_slot_6"] = "inventory",
  ["bag_slot_6_count"] = "inventory",
  ["bag_slot_6_equipped"] = "inventory",
  ["bag_slot_6_icon"] = "inventory",
  ["bag_slot_7"] = "inventory",
  ["bag_slot_7_count"] = "inventory",
  ["bag_slot_7_equipped"] = "inventory",
  ["bag_slot_7_icon"] = "inventory",
  ["bag_slot_8"] = "inventory",
  ["bag_slot_8_count"] = "inventory",
  ["bag_slot_8_equipped"] = "inventory",
  ["bag_slot_8_icon"] = "inventory",
  ["bag_slot_9"] = "inventory",
  ["bag_slot_9_count"] = "inventory",
  ["bag_slot_9_equipped"] = "inventory",
  ["bag_slot_9_icon"] = "inventory",
  ["bag_title"] = "inventory",
  ["bag_veil"] = "inventory",
  ["card"] = "dialogs",
  ["character_stats"] = "inventory",
  ["continue_button"] = "dialogs",
  ["dialog_portrait"] = "dialogs",
  ["equipment_panel"] = "inventory",
  ["equipped_armor"] = "inventory",
  ["equipped_armor_name"] = "inventory",
  ["equipped_charm"] = "inventory",
  ["equipped_charm_name"] = "inventory",
  ["equipped_weapon"] = "inventory",
  ["equipped_weapon_name"] = "inventory",
  ["item_description"] = "inventory",
  ["item_drop"] = "inventory",
  ["item_name"] = "inventory",
  ["item_sell"] = "inventory",
  ["item_use"] = "inventory",
  ["journal_text"] = "inventory",
  ["overlay"] = "dialogs",
  ["overlay_detail"] = "dialogs",
  ["overlay_subtitle"] = "dialogs",
  ["overlay_title"] = "dialogs",
  ["power_choice"] = "dialogs",
  ["reset_button"] = "dialogs",
  ["restart_button"] = "dialogs",
  ["resume_button"] = "dialogs",
  ["save_button"] = "dialogs",
  ["shop_item_1"] = "inventory",
  ["shop_item_2"] = "inventory",
  ["shop_item_3"] = "inventory",
  ["shop_item_4"] = "inventory",
  ["shop_item_5"] = "inventory",
  ["shop_item_6"] = "inventory",
  ["shop_stock"] = "inventory",
  ["start_button"] = "dialogs",
  ["title_button"] = "dialogs",
  ["veil"] = "dialogs",
  ["vitality_choice"] = "dialogs",
}

local boss =
  { boss_panel = "boss_bar", boss_name = "boss_bar.boss_name", boss_fill = "boss_bar.boss_fill" }

function Ui.id(id)
  if boss[id] then
    return boss[id]
  end
  if id == "overlay" then
    return "game_ui.dialogs"
  end
  if id == "bag" then
    return "game_ui.inventory"
  end
  if id == "root" then
    return "game_ui"
  end
  if id == "game_ui" or id:match("^game_ui%.") or id:match("^boss_bar") then
    return id
  end
  local scope = scopes[id:match("^[^%.]+") or id]
  return "game_ui." .. (scope and (scope .. ".") or "") .. id
end

function Ui.local_id(id)
  return (
    id:gsub("^game_ui%.inventory%.", ""):gsub("^game_ui%.dialogs%.", ""):gsub("^game_ui%.", "")
  )
end

for _, name in ipairs({
  "find",
  "get_text",
  "set_text",
  "set_visible",
  "set_disabled",
  "set_position",
  "set_size",
  "set_color",
  "set_background_color",
  "set_image",
  "clear_children",
}) do
  Ui[name] = function(id, ...)
    return Native[name](Ui.id(id), ...)
  end
end

function Ui.create(parent, definition)
  definition.id = Ui.id(definition.id)
  return Native.create(Ui.id(parent), definition)
end

return Ui
