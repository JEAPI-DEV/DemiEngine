local Floors = require("dungeon.floors")
local Combat = require("dungeon.combat")
local Enemies = require("dungeon.enemies")
local Inventory = require("dungeon.inventory")
local Loot = require("dungeon.loot")
local Encounters = require("dungeon.encounters")
local Exploration = require("dungeon.exploration")
local Magic = require("dungeon.magic")
local Run = {}

function Run.enter_floor(run, index)
  run.floor = Floors.create(index)
  run.enemies, run.projectiles, run.pickups = {}, {}, {}
  run.player.x, run.player.y = table.unpack(run.floor.spawn)
  run.player.invulnerable = 1
  run.player.dash_time = 0
  run.player.melee = nil
  run.phase = "playing"
  run.floor_time = 0
  for number, spawn in ipairs(run.floor.enemies) do
    run.enemies[number] = Enemies.create(spawn[1], spawn[2], spawn[3], number)
  end
  for number, point in ipairs(run.floor.potions) do
    run.pickups[number] = {
      id = "potion_" .. number,
      x = point[1],
      y = point[2],
      taken = false,
      item = "healing_potion",
      count = 1,
    }
  end
  Loot.populate(run)
  Encounters.create(run)
  Exploration.create(run)
end

function Run.new()
  local run = {
    player = {
      id = "player",
      hp = 8,
      max_hp = 8,
      power = 1,
      radius = 0.3,
      base_hp = 8,
      base_power = 1,
      base_mana = Magic.capacity,
      mana = Magic.capacity,
      max_mana = Magic.capacity,
      cast_slow = 0,
      level = 1,
      xp = 0,
      buff_time = 0,
      inventory = Inventory.new(),
      cooldown = 0,
      dash_cooldown = 0,
      dash_time = 0,
      flash = 0,
      aim_x = 0,
      aim_y = 1,
    },
    coins = 0,
    kills = 0,
    shots = 0,
    damage_taken = 0,
    time = 0,
    next_projectile = 0,
    next_loot = 0,
    events = {},
    phase = "playing",
    message = "Short sword equipped. Bait a swing, dodge aside, then counterattack. I opens your bag.",
  }
  Inventory.add(run.player.inventory, "short_sword", 1)
  Inventory.add(run.player.inventory, "linen", 1)
  Inventory.add(run.player.inventory, "healing_potion", 3)
  Inventory.add(run.player.inventory, "oak_staff", 1)
  Inventory.add(run.player.inventory, "mana_potion", 1)
  run.player.inventory.equipment = { weapon = 1, armor = 2 }
  Inventory.refresh(run.player)
  Run.enter_floor(run, 1)
  return run
end

function Run.remaining(run)
  local count = 0
  for _, enemy in ipairs(run.enemies) do
    if enemy.hp > 0 then
      count = count + 1
    end
  end
  return count
end

function Run.upgrade(run, choice)
  if run.phase ~= "upgrade" or (choice ~= "power" and choice ~= "vitality") then
    return false
  end
  if choice == "power" then
    run.player.base_power = run.player.base_power + 1
  else
    run.player.base_hp = run.player.base_hp + 3
  end
  Inventory.refresh(run.player)
  run.player.hp = math.min(run.player.max_hp, run.player.hp + 4)
  Run.enter_floor(run, run.floor.index + 1)
  return true
end

function Run.step(run, action, geometry, dt)
  run.events = {}
  run.interaction = nil
  Exploration.update(run, geometry)
  if run.phase ~= "playing" then
    return
  end
  run.time = run.time + dt
  run.floor_time = run.floor_time + dt
  if action.equip_slot and run.player.inventory.equipment.weapon ~= action.equip_slot then
    Inventory.activate(run.player, action.equip_slot)
  end
  Combat.update_player(run, action, geometry, dt)
  Enemies.update(run, geometry, dt)
  Combat.update_projectiles(run, geometry, dt)
  Encounters.update(run)
  if run.phase == "dead" then
    return
  end
  Loot.update(run, dt)
  -- Stairs take priority over nearby optional rewards or conversations.
  if run.floor.index < 4 and action.interact then
    local _, _, distance =
      Combat.direction(run.floor.exit[1] - run.player.x, run.floor.exit[2] - run.player.y)
    if distance < 1.5 then
      run.phase = "upgrade"
      return
    end
  elseif run.floor.index == 4 and Run.remaining(run) == 0 then
    run.phase = "victory"
    return
  end
  if action.interact then
    if Exploration.interact(run) then
      return
    end
    if Encounters.interact(run) then
      return
    end
    local interaction = Loot.interact(run)
    if interaction then
      run.interaction = interaction
    end
  end
end

-- A copied observation is safe for agents; editing it cannot change the game.
function Run.observe(run)
  local function copy(value)
    if type(value) ~= "table" then
      return value
    end
    local result = {}
    for key, child in pairs(value) do
      result[key] = copy(child)
    end
    return result
  end
  return copy({
    player = run.player,
    enemies = run.enemies,
    projectiles = run.projectiles,
    seals = run.seals,
    shrines = run.shrines,
    floor_time = run.floor_time,
    gates = run.gates,
    levers = run.levers,
    discovered = run.discovered,
    visible = run.visible,
    journal = run.journal,
    breakables = run.breakables,
    pickups = run.pickups,
    chests = run.chests,
    merchant = run.merchant,
    floor = run.floor.index,
    phase = run.phase,
    kills = run.kills,
    time = run.time,
    coins = run.coins,
    shots = run.shots,
    damage_taken = run.damage_taken,
  })
end

return Run
