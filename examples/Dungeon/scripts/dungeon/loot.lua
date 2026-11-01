local Inventory = require("dungeon.inventory")
local Items = require("dungeon.items")
local Exploration = require("dungeon.exploration")
local Magic = require("dungeon.magic")
local Loot = {}
Loot.stock =
  { "healing_potion", "mana_potion", "short_sword", "chainmail", "swift_boots", "long_sword" }

function Loot.drop(run, item, x, y, count)
  run.next_loot = run.next_loot + 1
  run.pickups[#run.pickups + 1] = {
    id = "loot_" .. run.next_loot,
    item = item,
    x = x,
    y = y,
    count = count or 1,
    taken = false,
  }
end

function Loot.populate(run)
  run.chests = {}
  for index, source in ipairs(run.floor.chests) do
    run.chests[index] = {
      id = "chest_" .. index,
      x = source[1],
      y = source[2],
      item = source[3],
      gate = source[4],
      opened = false,
    }
  end
  run.merchant = { x = run.floor.spawn[1] - 1.5, y = run.floor.spawn[2] - 1 }
end

function Loot.update(run, dt)
  for _, enemy in ipairs(run.enemies) do
    if enemy.hp <= 0 and not enemy.rewarded then
      enemy.rewarded = true
      local xp = enemy.kind == "boss" and 50 or (enemy.kind == "sentinel" and 10 or 6)
      run.player.xp = run.player.xp + xp
      while run.player.xp >= run.player.level * 20 do
        run.player.xp = run.player.xp - run.player.level * 20
        run.player.level = run.player.level + 1
        run.player.base_hp = run.player.base_hp + 1
        if run.player.level % 2 == 0 then
          run.player.base_power = run.player.base_power + 1
        end
        Inventory.refresh(run.player)
        run.player.hp = math.min(run.player.max_hp, run.player.hp + 2)
        run.message = "Level " .. run.player.level .. "! Vitality and spellcraft increased."
      end
      if enemy.kind == "acolyte" then
        Loot.drop(run, "mana_shard", enemy.x, enemy.y)
      elseif enemy.kind == "sentinel" then
        Loot.drop(run, "healing_potion", enemy.x, enemy.y)
      end
    end
  end
  for _, prop in ipairs(run.breakables or {}) do
    if prop.hp <= 0 and not prop.rewarded then
      prop.rewarded = true
      Loot.drop(run, prop.loot, prop.x, prop.y)
    end
  end
  for _, pickup in ipairs(run.pickups) do
    pickup.delay = math.max(0, (pickup.delay or 0) - dt)
    local distance = (pickup.x - run.player.x) ^ 2 + (pickup.y - run.player.y) ^ 2
    if not pickup.taken and pickup.delay == 0 and distance < 0.7 ^ 2 then
      local item = Items[pickup.item]
      local added, message
      if item.pickup == "mana" then
        if run.player.mana < run.player.max_mana then
          Magic.restore(run.player, item.amount)
          added = true
        end
      else
        added, message = Inventory.add(run.player.inventory, pickup.item, pickup.count or 1)
      end
      if added then
        pickup.taken = true
        run.message = "Collected " .. Items[pickup.item].name .. "."
      else
        run.message = message or "Ether remains here until you need mana."
      end
    end
  end
end

function Loot.interact(run)
  for _, chest in ipairs(run.chests) do
    local distance = (chest.x - run.player.x) ^ 2 + (chest.y - run.player.y) ^ 2
    if not chest.opened and distance < 1.5 ^ 2 and Exploration.gate_open(run, chest.gate) then
      local added, message = Inventory.add(run.player.inventory, chest.item, 1)
      if added then
        chest.opened = true
        run.message = "Found " .. Items[chest.item].name .. "! Open your bag with I."
      else
        run.message = message
      end
      return "chest"
    end
  end
  local distance = (run.merchant.x - run.player.x) ^ 2 + (run.merchant.y - run.player.y) ^ 2
  if distance < 1.5 ^ 2 then
    return "shop"
  end
end

return Loot
