local Hud = require("dungeon.ui")
local Items = require("dungeon.items")
local Inventory = require("dungeon.inventory")
local Loot = require("dungeon.loot")
local Menus = {}
Menus.__index = Menus

function Menus.new()
  return setmetatable({ mode = nil, rendered_mode = false, selected = 1, dirty = true }, Menus)
end

function Menus:toggle()
  if self.mode then
    self.mode = nil
  else
    self.mode = "inventory"
  end
end

function Menus:open_shop()
  self.mode = "shop"
end

function Menus:select(event)
  local index = event.id and Hud.local_id(event.id):match("^bag_slot_(%d+)%.hit$")
  if index then
    self.selected = tonumber(index)
    self.dirty = true
  end
end

function Menus:use(run)
  self.dirty = true
  local used, message = Inventory.activate(run.player, self.selected)
  run.message = message
  return used
end

function Menus:drop(run)
  self.dirty = true
  local id, message = Inventory.remove(run.player.inventory, self.selected)
  if not id then
    run.message = message
    return
  end
  Loot.drop(run, id, run.player.x, run.player.y)
  run.pickups[#run.pickups].delay = 1.5
  run.message = "Dropped " .. Items[id].name .. ". Move away to leave it behind."
end

function Menus:sell(run)
  self.dirty = true
  if self.mode ~= "shop" then
    return
  end
  local _, message = Inventory.sell(run, self.selected)
  run.message = message
end

function Menus:buy(run, event)
  self.dirty = true
  if self.mode ~= "shop" then
    return
  end
  local index = event.id and Hud.local_id(event.id):match("^shop_item_(%d+)%.hit$")
  local id = index and Loot.stock[tonumber(index)]
  if id then
    local _, message = Inventory.buy(run, id)
    run.message = message
  end
end

local function icon(id, item)
  Hud.set_image(
    id,
    item.texture or "asset://dungeon/buttons",
    item.icon[1],
    item.icon[2],
    item.icon_size or 24,
    item.icon_size or 24
  )
end

function Menus:update(run)
  if self.rendered_mode ~= self.mode then
    Hud.set_visible("bag", self.mode ~= nil)
    self.rendered_mode = self.mode
    self.dirty = true
  end
  if not self.mode then
    return
  end
  if self.last_message ~= run.message then
    self.dirty = true
  end
  if not self.dirty then
    return
  end
  self.dirty = false
  self.last_message = run.message
  local bag = run.player.inventory
  local shop = self.mode == "shop"
  local journal = self.mode == "journal"
  Hud.set_text(
    "bag_title",
    journal and "DISCOVERIES & NOTES" or (shop and "THE WAYFARER'S EXCHANGE" or "PACK & CHARACTER")
  )
  Hud.set_text("bag_message", run.message)
  Hud.set_visible("shop_stock", shop)
  Hud.set_visible("character_stats", not shop and not journal)
  Hud.set_visible("equipment_panel", not shop and not journal)
  Hud.set_visible("item_description", not shop and not journal)
  Hud.set_visible("item_name", not shop and not journal)
  Hud.set_visible("item_sell", shop)
  Hud.set_visible("item_use", not journal)
  Hud.set_visible("item_drop", not journal)
  Hud.set_visible("journal_text", journal)
  Hud.set_text(
    "journal_text",
    #run.journal > 0 and table.concat(run.journal, "\n\n")
      or "Read keeper notes and investigate unusual walls. Your discoveries will be recorded here."
  )
  local occupied = 0
  for index = 1, Inventory.capacity do
    local entry = bag.slots[index]
    local id = "bag_slot_" .. index
    Hud.set_visible(id, not journal)
    Hud.set_visible(id .. "_equipped", not journal)
    Hud.set_visible(id .. "_count", not journal)
    Hud.set_visible(id .. "_icon", entry ~= nil and not journal)
    Hud.set_text(id .. "_count", entry and entry.count > 1 and tostring(entry.count) or "")
    Hud.set_color(
      id,
      index == self.selected and 1 or 0.55,
      index == self.selected and 0.86 or 0.63,
      index == self.selected and 0.6 or 0.78,
      1
    )
    local equipped = false
    for _, slot in pairs(bag.equipment) do
      equipped = equipped or slot == index
    end
    Hud.set_text(id .. "_equipped", equipped and "EQUIPPED" or "")
    if entry then
      occupied = occupied + 1
      icon(id .. "_icon", Items[entry.item])
    end
  end
  Hud.set_text(
    "bag_capacity",
    string.format("PACK %d / %d    SOULS %d", occupied, Inventory.capacity, run.coins)
  )
  local stats = Inventory.stats(run.player)
  Hud.set_text(
    "character_stats",
    string.format(
      "LEVEL %d   XP %d / %d\nDAMAGE %d   DEFENSE %d%%\nVITALITY %.1f / %d",
      run.player.level,
      run.player.xp,
      run.player.level * 20,
      stats.power,
      stats.defense * 15,
      run.player.hp,
      run.player.max_hp
    )
  )
  for _, slot in ipairs({ "weapon", "armor", "charm" }) do
    local entry = bag.slots[bag.equipment[slot]]
    Hud.set_visible("equipped_" .. slot, entry ~= nil)
    Hud.set_text(
      "equipped_" .. slot .. "_name",
      entry and Items[entry.item].name or "Empty " .. slot
    )
    if entry then
      icon("equipped_" .. slot, Items[entry.item])
    end
  end
  local entry = bag.slots[self.selected]
  local item = entry and Items[entry.item]
  Hud.set_text("item_name", item and item.name or "Empty slot")
  Hud.set_text(
    "item_description",
    item and item.description or "Open chests and collect drops to fill your bag."
  )
  local equipped = item and item.slot and bag.equipment[item.slot] == self.selected
  Hud.set_text("item_use.hit", equipped and "UNEQUIP" or (item and item.slot and "EQUIP" or "USE"))
  Hud.set_disabled("item_use.hit", not item)
  Hud.set_disabled("item_drop.hit", not item or equipped or false)
  Hud.set_disabled("item_sell.hit", not item or equipped or false)
  for index, id in ipairs(Loot.stock) do
    local stock = Items[id]
    Hud.set_text("shop_item_" .. index .. ".hit", stock.name .. "\n" .. stock.price .. " souls")
    Hud.set_disabled("shop_item_" .. index .. ".hit", run.coins < stock.price)
  end
end

return Menus
