local Items = require("dungeon.items")
local Melee = require("dungeon.melee")
local Magic = require("dungeon.magic")
local Inventory = {}
Inventory.capacity = 12

function Inventory.new()
  return { slots = {}, equipment = {} }
end

function Inventory.add(bag, id, count)
  local item = Items[id]
  if not item or item.pickup or type(count) ~= "number" or count < 1 or count % 1 ~= 0 then
    return false, "Invalid item quantity"
  end
  local stack = item.stack or 1
  local capacity = 0
  for index = 1, Inventory.capacity do
    local entry = bag.slots[index]
    if not entry then
      capacity = capacity + stack
    elseif entry.item == id then
      capacity = capacity + stack - entry.count
    end
  end
  if capacity < count then
    return false, "Your bag is full. Sell or drop an item."
  end
  -- Fill existing stacks first, then empty slots. Failed additions change nothing.
  for pass = 1, 2 do
    for index = 1, Inventory.capacity do
      local entry = bag.slots[index]
      local suitable = pass == 1 and entry and entry.item == id
      if suitable or (pass == 2 and not entry) then
        local amount = math.min(count, stack - (entry and entry.count or 0))
        if amount > 0 then
          bag.slots[index] = { item = id, count = (entry and entry.count or 0) + amount }
          count = count - amount
        end
      end
      if count == 0 then
        return true
      end
    end
  end
end

function Inventory.count(bag, id)
  local count = 0
  for index = 1, Inventory.capacity do
    local entry = bag.slots[index]
    if entry and entry.item == id then
      count = count + entry.count
    end
  end
  return count
end

function Inventory.remove(bag, index)
  local entry = bag.slots[index]
  if not entry then
    return nil, "Select an item first."
  end
  for _, equipped in pairs(bag.equipment) do
    if equipped == index then
      return nil, "Unequip this item before removing it."
    end
  end
  local id = entry.item
  entry.count = entry.count - 1
  if entry.count == 0 then
    bag.slots[index] = nil
  end
  return id
end

function Inventory.stats(player)
  local stats = {
    power = player.base_power,
    max_hp = player.base_hp,
    max_mana = player.base_mana or Magic.capacity,
    defense = 0,
    speed = 4.6,
    interval = 0.28,
    dash_delay = 1.15,
    weapon = "unarmed",
  }
  for slot, index in pairs(player.inventory.equipment) do
    local entry = player.inventory.slots[index]
    local item = entry and Items[entry.item]
    if item and item.slot == slot then
      stats.power = stats.power + (item.power or 0)
      stats.max_hp = stats.max_hp + (item.vitality or 0)
      stats.defense = stats.defense + (item.defense or 0)
      stats.speed = stats.speed + (item.speed or 0)
      if item.melee then
        local profile = Melee.profiles[item.melee]
        stats.interval = profile.windup + profile.active + profile.recovery
      elseif item.spell then
        stats.interval = Magic.profiles[item.spell].interval
      else
        stats.interval = item.interval or stats.interval
      end
      if slot == "weapon" then
        stats.weapon = entry.item
      end
      if item.speed then
        stats.dash_delay = 0.85
      end
    end
  end
  if (player.buff_time or 0) > 0 then
    stats.power = stats.power + 2
  end
  return stats
end

function Inventory.refresh(player)
  local stats = Inventory.stats(player)
  player.power, player.max_hp = stats.power, stats.max_hp
  player.hp = math.min(player.hp, player.max_hp)
  player.max_mana = stats.max_mana
  player.mana = math.min(player.mana or player.max_mana, player.max_mana)
  return stats
end

function Inventory.activate(player, index)
  local bag = player.inventory
  local entry = bag.slots[index]
  local item = entry and Items[entry.item]
  if not item then
    return false, "Select an item first."
  end
  if item.slot then
    if item.slot == "weapon" then
      -- The bag pauses simulation, so a swing cannot finish while it is open.
      -- Cancel its remaining hits, but retain cooldown to prevent free attacks.
      player.melee = nil
    end
    if bag.equipment[item.slot] == index then
      bag.equipment[item.slot] = nil
    else
      bag.equipment[item.slot] = index
    end
    Inventory.refresh(player)
    return true, item.name .. (bag.equipment[item.slot] and " equipped." or " unequipped.")
  end
  if item.heal then
    if player.hp >= player.max_hp then
      return false, "Vitality is already full."
    end
    player.hp = math.min(player.max_hp, player.hp + item.heal)
  elseif item.mana then
    if player.mana >= player.max_mana then
      return false, "Mana is already full."
    end
    Magic.restore(player, item.mana)
  elseif item.buff then
    player.buff_time = 12
  else
    return false, "This item cannot be used."
  end
  assert(Inventory.remove(bag, index))
  Inventory.refresh(player)
  return true, item.name .. " used."
end

function Inventory.cycle_weapon(player)
  local bag = player.inventory
  local current = bag.equipment.weapon or 0
  for offset = 1, Inventory.capacity do
    local index = (current + offset - 1) % Inventory.capacity + 1
    local entry = bag.slots[index]
    if entry and Items[entry.item].slot == "weapon" then
      bag.equipment.weapon = index
      Inventory.refresh(player)
      return Items[entry.item].name .. " equipped."
    end
  end
  return "No weapon in your bag."
end

function Inventory.heal(player)
  for index = 1, Inventory.capacity do
    local entry = player.inventory.slots[index]
    if entry and entry.item == "healing_potion" then
      return Inventory.activate(player, index)
    end
  end
  return false, "No healing potions in your bag."
end

function Inventory.restore_mana(player)
  for index = 1, Inventory.capacity do
    local entry = player.inventory.slots[index]
    if entry and entry.item == "mana_potion" then
      return Inventory.activate(player, index)
    end
  end
  return false, "No mana flasks. Land a melee hit or explore for supplies."
end

function Inventory.buy(run, id)
  local item = Items[id]
  if not item or not item.price or run.coins < item.price then
    return false, "Not enough souls."
  end
  local added, message = Inventory.add(run.player.inventory, id, 1)
  if not added then
    return false, message
  end
  run.coins = run.coins - item.price
  return true, "Bought " .. item.name .. "."
end

function Inventory.sell(run, index)
  local id, message = Inventory.remove(run.player.inventory, index)
  if not id then
    return false, message
  end
  run.coins = run.coins + math.max(1, math.floor(Items[id].price / 2))
  return true, "Sold " .. Items[id].name .. "."
end

return Inventory
