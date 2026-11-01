local Run = require("dungeon.run")
local Floors = require("dungeon.floors")
local Items = require("dungeon.items")
local Inventory = require("dungeon.inventory")
local Melee = require("dungeon.melee")
local Encounters = require("dungeon.encounters")
local Exploration = require("dungeon.exploration")
local Magic = require("dungeon.magic")
local Checkpoint = {}

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

function Checkpoint.encode(run)
  local player = copy(run.player)
  -- Sparse numeric bag keys must not become JSON object keys on round-trip.
  player.inventory.slots = {}
  for index = 1, Inventory.capacity do
    local entry = run.player.inventory.slots[index]
    if entry then
      player.inventory.slots[#player.inventory.slots + 1] = {
        index = index,
        item = entry.item,
        count = entry.count,
      }
    end
  end
  local enemies = copy(run.enemies)
  for _, enemy in ipairs(enemies) do
    enemy.path, enemy.path_index = nil, nil
    enemy.path_timer = 0
  end
  return {
    format_version = 3,
    floor = run.floor.index,
    phase = run.phase,
    gates = copy(run.gates),
    levers = copy(run.levers),
    clues = copy(run.clues),
    breakables = copy(run.breakables),
    discovered = copy(run.discovered),
    visited_rooms = copy(run.visited_rooms),
    journal = copy(run.journal),
    seals = copy(run.seals),
    shrines = copy(run.shrines),
    trap_hits = copy(run.trap_hits),
    player = player,
    enemies = enemies,
    pickups = copy(run.pickups),
    chests = copy(run.chests),
    projectiles = copy(run.projectiles),
    coins = run.coins,
    kills = run.kills,
    shots = run.shots,
    damage_taken = run.damage_taken,
    time = run.time,
    floor_time = run.floor_time,
    next_projectile = run.next_projectile,
    next_loot = run.next_loot,
  }
end

local function number(value, minimum, maximum, integer)
  return type(value) == "number"
    and value == value
    and value >= minimum
    and value <= maximum
    and (not integer or value % 1 == 0)
end

function Checkpoint.decode(saved)
  local valid, result = pcall(function()
    local migrated = saved.format_version == 1 or saved.format_version == 2
    if migrated then
      assert(number(saved.floor, 1, 4, true), "Invalid legacy floor.")
      local fresh = Run.new()
      Run.enter_floor(fresh, saved.floor)
      local converted = Checkpoint.encode(fresh)
      converted.player = copy(saved.player)
      for key, value in pairs(fresh.player) do
        if type(value) == "number" and converted.player[key] == nil then
          converted.player[key] = value
        end
      end
      for _, entry in ipairs(converted.player.inventory.slots) do
        if entry.item == "iron_wand" then
          entry.item = "short_sword"
        elseif entry.item == "ash_staff" then
          entry.item = "long_sword"
        end
      end
      converted.player.x, converted.player.y = table.unpack(fresh.floor.spawn)
      converted.player.melee = nil
      converted.player.cooldown, converted.player.dash_time = 0, 0
      for _, key in ipairs({ "coins", "kills", "shots", "damage_taken", "time" }) do
        converted[key] = saved[key]
      end
      saved = converted
    end
    assert(saved.format_version == 3, "Unsupported checkpoint version; existing save retained.")
    assert(number(saved.floor, 1, 4, true), "Invalid floor.")
    assert(saved.phase == "playing" or saved.phase == "upgrade", "Checkpoint is not resumable.")
    assert(saved.phase ~= "upgrade" or saved.floor < 4, "Invalid blessing screen.")
    local run = Run.new()
    Run.enter_floor(run, saved.floor)
    for _, key in ipairs({
      "coins",
      "kills",
      "shots",
      "damage_taken",
      "time",
      "floor_time",
      "next_projectile",
      "next_loot",
    }) do
      local integral = key == "coins"
        or key == "kills"
        or key == "shots"
        or key == "next_projectile"
        or key == "next_loot"
      assert(number(saved[key], 0, 100000000, integral), "Invalid " .. key)
      run[key] = saved[key]
    end
    local player = copy(saved.player)
    for key, baseline in pairs(run.player) do
      if type(baseline) == "number" then
        assert(number(player[key], -100000, 100000), "Invalid player " .. key)
      end
    end
    assert(
      number(player.base_mana, 1, 1000) and number(player.mana, 0, player.base_mana),
      "Invalid mana state."
    )
    assert(player.id == "player" and player.radius == 0.3, "Invalid player identity.")
    assert(
      number(player.base_hp, 1, 10000) and number(player.base_power, 1, 1000),
      "Invalid base stats."
    )
    assert(number(player.level, 1, 100, true) and player.xp >= 0, "Invalid experience.")
    assert(Floors.walkable(run.floor, player.x, player.y), "Player is outside the map.")
    local bag = Inventory.new()
    for _, entry in ipairs(player.inventory.slots) do
      assert(number(entry.index, 1, Inventory.capacity, true), "Invalid bag slot.")
      assert(
        not bag.slots[entry.index] and Items[entry.item] and not Items[entry.item].pickup,
        "Invalid or duplicate item."
      )
      assert(number(entry.count, 1, Items[entry.item].stack or 1, true), "Invalid stack.")
      bag.slots[entry.index] = { item = entry.item, count = entry.count }
    end
    for slot, index in pairs(player.inventory.equipment) do
      local entry = bag.slots[index]
      assert(entry and Items[entry.item].slot == slot, "Invalid equipment reference.")
      bag.equipment[slot] = index
    end
    player.inventory = bag
    Inventory.refresh(player)
    assert(player.hp > 0, "Checkpoint player is dead.")
    run.player = player
    assert(
      #saved.enemies == #run.enemies and #saved.chests == #run.chests,
      "Encounter layout changed."
    )
    for index, enemy in ipairs(saved.enemies) do
      local expected = run.enemies[index]
      assert(enemy.id == expected.id and enemy.kind == expected.kind, "Enemy identity changed.")
      for key, baseline in pairs(expected) do
        if type(baseline) == "number" then
          assert(number(enemy[key], -100000, 100000), "Invalid enemy " .. key)
        end
      end
      assert(
        enemy.max_hp == expected.max_hp and number(enemy.hp, 0, expected.max_hp),
        "Invalid enemy health."
      )
      assert(Floors.walkable(run.floor, enemy.x, enemy.y), "Enemy outside map.")
    end
    for index, chest in ipairs(saved.chests) do
      local expected = run.chests[index]
      assert(
        chest.id == expected.id
          and chest.item == expected.item
          and chest.gate == expected.gate
          and chest.x == expected.x
          and chest.y == expected.y,
        "Invalid chest."
      )
      assert(type(chest.opened) == "boolean", "Invalid chest state.")
    end
    local ids = {}
    for _, pickup in ipairs(saved.pickups) do
      assert(
        type(pickup.id) == "string" and pickup.id:match("^[%w_]+$") and not ids[pickup.id],
        "Invalid loot ID."
      )
      local dropped = tonumber(pickup.id:match("^loot_(%d+)$"))
      local authored = tonumber(pickup.id:match("^potion_(%d+)$"))
      assert(
        (dropped and dropped >= 1 and dropped <= run.next_loot)
          or (authored and authored >= 1 and authored <= #run.floor.potions),
        "Invalid loot identity."
      )
      ids[pickup.id] = true
      assert(Items[pickup.item] and number(pickup.count or 1, 1, 9, true), "Invalid loot.")
      assert(
        number(pickup.x, 0, run.floor.width) and number(pickup.y, 0, run.floor.height),
        "Invalid loot position."
      )
      assert(type(pickup.taken) == "boolean", "Invalid loot state.")
    end
    for _, bolt in ipairs(saved.projectiles) do
      assert(
        type(bolt.id) == "string" and bolt.id:match("^bolt_%d+$") and not ids[bolt.id],
        "Invalid projectile ID."
      )
      assert(tonumber(bolt.id:match("%d+$")) <= run.next_projectile, "Invalid projectile sequence.")
      ids[bolt.id] = true
      for _, key in ipairs({ "x", "y", "dx", "dy", "speed", "damage", "life" }) do
        assert(number(bolt[key], -1000, 1000), "Invalid projectile.")
      end
      assert(not bolt.spell or Magic.profiles[bolt.spell], "Unknown projectile spell.")
      assert(type(bolt.hostile) == "boolean", "Invalid projectile owner.")
    end
    run.enemies, run.chests = copy(saved.enemies), copy(saved.chests)
    run.pickups, run.projectiles = copy(saved.pickups), copy(saved.projectiles)
    for _, group in ipairs({ "seals", "shrines" }) do
      assert(
        type(saved[group]) == "table" and #saved[group] == #run[group],
        "Invalid encounter state."
      )
      for index, item in ipairs(saved[group]) do
        local expected = run[group][index]
        assert(
          item.id == expected.id and item.x == expected.x and item.y == expected.y,
          "Encounter identity changed."
        )
        if group == "seals" then
          assert(item.guard == expected.guard and type(item.claimed) == "boolean", "Invalid sigil.")
        else
          assert(type(item.used) == "boolean", "Invalid shrine.")
        end
      end
      run[group] = copy(saved[group])
    end
    assert(type(saved.trap_hits) == "table", "Invalid trap state.")
    for key, cycle in pairs(saved.trap_hits) do
      assert(type(key) == "string" and number(cycle, 0, 100000000, true), "Invalid trap cycle.")
    end
    run.trap_hits = copy(saved.trap_hits)
    local actors = { run.player }
    for _, enemy in ipairs(run.enemies) do
      actors[#actors + 1] = enemy
    end
    for _, actor in ipairs(actors) do
      if actor.melee then
        local attack = actor.melee
        local spec = Melee.profiles[attack.profile]
        assert(
          spec and number(attack.angle, -1000, 1000) and number(attack.damage, 0, 10000),
          "Invalid melee attack."
        )
        assert(
          number(attack.elapsed, 0, spec.windup + spec.active + spec.recovery),
          "Invalid attack time."
        )
        assert(
          attack.phase == "windup" or attack.phase == "active" or attack.phase == "recovery",
          "Invalid attack phase."
        )
        for id, hit in pairs(attack.hit) do
          assert(type(id) == "string" and type(hit) == "boolean", "Invalid attack hit history.")
        end
      end
    end
    for _, group in ipairs({ "gates", "levers", "clues", "breakables" }) do
      assert(
        type(saved[group]) == "table" and #saved[group] == #run[group],
        "Exploration layout changed."
      )
      for index, stored in ipairs(saved[group]) do
        local item = run[group][index]
        assert(
          stored.id == item.id and stored.x == item.x and stored.y == item.y,
          "Exploration identity changed."
        )
        if group == "gates" then
          assert(
            type(stored.open) == "boolean"
              and stored.width == item.width
              and stored.height == item.height,
            "Invalid gate."
          )
          item.open = stored.open
        elseif group == "levers" then
          assert(type(stored.used) == "boolean", "Invalid lever.")
          item.used = stored.used
        elseif group == "clues" then
          assert(type(stored.read) == "boolean", "Invalid note.")
          item.read = stored.read
        else
          assert(
            number(stored.hp, 0, 1) and type(stored.rewarded) == "boolean",
            "Invalid breakable."
          )
          item.hp, item.rewarded = stored.hp, stored.rewarded
        end
      end
    end
    assert(
      type(saved.discovered) == "table"
        and type(saved.visited_rooms) == "table"
        and type(saved.journal) == "table",
      "Invalid discovery data."
    )
    for key, value in pairs(saved.discovered) do
      local x, y = tostring(key):match("^(%d+):(%d+)$")
      assert(
        x
          and y
          and tonumber(x) < run.floor.width
          and tonumber(y) < run.floor.height
          and value == true,
        "Invalid discovery cell."
      )
    end
    for key, value in pairs(saved.visited_rooms) do
      assert(type(key) == "string" and value == true, "Invalid discovered room.")
    end
    for _, note in ipairs(saved.journal) do
      assert(type(note) == "string", "Invalid journal.")
    end
    run.discovered, run.visited_rooms, run.journal =
      copy(saved.discovered), copy(saved.visited_rooms), copy(saved.journal)
    for _, chest in ipairs(run.chests) do
      assert(
        not chest.opened or Exploration.gate_open(run, chest.gate),
        "Opened chest inside a closed vault."
      )
    end
    run.phase = saved.phase
    run.message = migrated
        and "Layout updated: your gear and progression are kept; this floor restarts at its entrance."
      or "Checkpoint restored."
    return run
  end)
  if not valid then
    return nil, "Cannot resume: " .. tostring(result)
  end
  return result
end

return Checkpoint
