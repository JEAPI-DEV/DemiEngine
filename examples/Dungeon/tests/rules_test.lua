-- Run from the repository root: lua examples/Dungeon/tests/rules_test.lua
package.path = "examples/Dungeon/scripts/?.lua;" .. package.path
local layouts = assert(io.popen("python3 examples/Dungeon/tools/export_layouts_for_tests.py"))
local layout_source = layouts:read("*a")
assert(layouts:close())
package.preload["demi.data"] = function()
  return {
    load = function(id)
      -- Native Data.load also returns a detached snapshot on each call.
      return assert(load(layout_source, "authored layouts", "t", {}))()[id]
    end,
  }
end
local Floors = require("dungeon.floors")
local Combat = require("dungeon.combat")
local Run = require("dungeon.run")
local Enemies = require("dungeon.enemies")
local Inventory = require("dungeon.inventory")
local Items = require("dungeon.items")
local Body = require("dungeon.body")
local Loot = require("dungeon.loot")
local Checkpoint = require("dungeon.checkpoint")
local Melee = require("dungeon.melee")
local Encounters = require("dungeon.encounters")
local Magic = require("dungeon.magic")
local Exploration = require("dungeon.exploration")

local passed = 0
local function test(name, body)
  local ok, message = pcall(body)
  if not ok then
    error(name .. ": " .. tostring(message))
  end
  passed = passed + 1
  print("PASS " .. name)
end

-- Domain tests use an unobstructed movement adapter; wall collision is exercised
-- separately in the real runtime, through Rigidbody2D.move_and_slide.
local open_space = {}
function open_space:move(actor, x, y)
  actor.x, actor.y = actor.x + x, actor.y + y
end
function open_space:wall_distance()
  return nil
end
function open_space:path()
  return {}
end

test("all floors connect every spawn, pickup and stair", function()
  for index = 1, 4 do
    local floor = Floors.create(index)
    local queue = { { floor.spawn[1], floor.spawn[2] } }
    local seen = {}
    local head = 1
    while queue[head] do
      local point = queue[head]
      head = head + 1
      local key = point[1] .. ":" .. point[2]
      if not seen[key] and Floors.walkable(floor, point[1], point[2]) then
        seen[key] = true
        for _, direction in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) do
          queue[#queue + 1] = { point[1] + direction[1], point[2] + direction[2] }
        end
      end
    end
    assert(seen[floor.exit[1] .. ":" .. floor.exit[2]])
    for _, enemy in ipairs(floor.enemies) do
      assert(seen[enemy[2] .. ":" .. enemy[3]])
    end
    for _, potion in ipairs(floor.potions) do
      assert(seen[potion[1] .. ":" .. potion[2]])
    end
  end
end)

test("diagonal movement is normalized and dash has a cooldown", function()
  local a, b = Run.new(), Run.new()
  Combat.update_player(a, { move_x = 1 }, open_space, 0.1)
  Combat.update_player(b, { move_x = 1, move_y = 1 }, open_space, 0.1)
  local _, _, moved = Combat.direction(b.player.x - 5, b.player.y - 5)
  assert(math.abs(moved - (a.player.x - 5)) < 0.00001)
  Combat.update_player(a, { dash = true, move_x = 1 }, open_space, 0.01)
  local cooldown = a.player.dash_cooldown
  Combat.update_player(a, { dash = true, move_x = 1 }, open_space, 0.01)
  assert(a.player.dash_cooldown < cooldown)
end)

test("damage, invulnerability and death are bounded", function()
  local run = Run.new()
  run.player.invulnerable = 0
  assert(Combat.damage(run, run.player, 2))
  assert(run.player.hp == 6)
  assert(not Combat.damage(run, run.player, 2))
  run.player.invulnerable = 0
  Combat.damage(run, run.player, 100)
  assert(run.player.hp == 0 and run.phase == "dead")
  local time = run.time
  Run.step(run, { attack = true }, open_space, 1 / 60)
  assert(run.time == time)
end)

test("swept projectiles hit the nearest enemy only and award one kill", function()
  local run = Run.new()
  run.enemies = { Enemies.create("crawler", 6, 5, 1), Enemies.create("crawler", 7, 5, 2) }
  Combat.fire(run, run.player, 1, 0, false, 100, 4)
  Combat.update_projectiles(run, open_space, 0.1)
  assert(run.enemies[1].hp == 0 and run.enemies[2].hp == 4)
  assert(run.kills == 1 and run.coins == 3 and #run.projectiles == 0)
  assert(not Combat.damage(run, run.enemies[1], 4))
  assert(run.kills == 1)
end)

test("wall occlusion stops bolts before enemies", function()
  local run = Run.new()
  run.enemies = { Enemies.create("crawler", 8, 5, 1) }
  local wall = {
    wall_distance = function()
      return 0.5
    end,
  }
  Combat.fire(run, run.player, 1, 0, false, 100, 10)
  Combat.update_projectiles(run, wall, 0.1)
  assert(run.enemies[1].hp == 4 and #run.projectiles == 0)
end)

test("stairs allow descent with every enemy alive and no sigils claimed", function()
  local run = Run.new()
  run.player.x, run.player.y = table.unpack(run.floor.exit)
  Run.step(run, {}, open_space, 1 / 60)
  assert(run.phase == "playing")
  Run.step(run, { interact = true }, open_space, 1 / 60)
  assert(run.phase == "upgrade" and run.kills == 0)
  assert(Run.remaining(run) == #run.enemies)
  for _, seal in ipairs(run.seals) do
    assert(not seal.claimed)
  end
  assert(Run.upgrade(run, "power") and run.floor.index == 2)
end)

test("potions enter inventory, require use and are not duplicated", function()
  local run = Run.new()
  run.player.x, run.player.y = run.pickups[1].x, run.pickups[1].y
  Run.step(run, {}, open_space, 1 / 60)
  assert(run.pickups[1].taken and Inventory.count(run.player.inventory, "healing_potion") == 4)
  assert(not Inventory.heal(run.player))
  run.player.hp = 2
  Run.step(run, { heal = true }, open_space, 1 / 60)
  assert(run.player.hp == 6 and Inventory.count(run.player.inventory, "healing_potion") == 3)
  Run.step(run, {}, open_space, 1 / 60)
  assert(Inventory.count(run.player.inventory, "healing_potion") == 3)
end)

test("boss telegraphs before firing and death ends the run", function()
  local run = Run.new()
  Run.enter_floor(run, 4)
  local boss = run.enemies[1]
  run.player.x, run.player.y = 16, 11
  boss.cooldown = 0
  Enemies.update(run, open_space, 1 / 60)
  assert(boss.windup > 0 and #run.projectiles == 0)
  for tick = 1, 60 do
    Enemies.update(run, open_space, 1 / 60)
  end
  assert(#run.projectiles == 12)
  Combat.damage(run, boss, boss.max_hp)
  Run.step(run, {}, open_space, 1 / 60)
  assert(run.phase == "victory")
end)

test("observations are detached and replayed actions produce matching rules", function()
  local a, b = Run.new(), Run.new()
  local observation = Run.observe(a)
  observation.player.hp = 0
  assert(a.player.hp == 8)
  for tick = 1, 240 do
    local action = { move_x = math.sin(tick / 50), move_y = 1, attack = true, aim_x = 8, aim_y = 8 }
    Run.step(a, action, open_space, 1 / 60)
    Run.step(b, action, open_space, 1 / 60)
  end
  assert(a.player.x == b.player.x and a.player.hp == b.player.hp)
  assert(a.kills == b.kills and a.shots == b.shots)
end)

test("combat covers feet, torso and head at every rendered actor scale", function()
  for _, kind in ipairs({ "crawler", "acolyte", "sentinel", "boss" }) do
    local actor = Enemies.create(kind, 5, 5, 1)
    local left, bottom, right, top = Body.bounds(actor)
    for _, y in ipairs({ bottom + 0.05, (top + bottom) / 2, top - 0.05 }) do
      local bolt = { x = 0, y = y, dx = 1, dy = 0 }
      local hit = Body.intersection(bolt, actor, 20)
      assert(hit and math.abs(hit - (left - 0.125)) < 0.00001)
    end
    assert(not Body.intersection({ x = 0, y = top + 0.2, dx = 1, dy = 0 }, actor, 20))
    assert(not Body.intersection({ x = right + 0.2, y = 0, dx = 0, dy = 1 }, actor, 20))
  end
end)

test("inventory stacking and capacity failures are atomic", function()
  local bag = Inventory.new()
  assert(Inventory.add(bag, "healing_potion", 10))
  assert(bag.slots[1].count == 9 and bag.slots[2].count == 1)
  for index = 3, 12 do
    assert(Inventory.add(bag, "chainmail", 1))
  end
  assert(not Inventory.add(bag, "healing_potion", 9))
  assert(Inventory.count(bag, "healing_potion") == 10)
  assert(not Inventory.add(bag, "unknown", 1))
  assert(not Inventory.add(bag, "healing_potion", -1))
end)

test("equipment changes real stats and cannot be sold while worn", function()
  local run = Run.new()
  assert(Inventory.add(run.player.inventory, "long_sword", 1))
  assert(Inventory.activate(run.player, 6))
  assert(run.player.power == 4 and math.abs(Inventory.stats(run.player).interval - 1.45) < 0.000001)
  assert(not Inventory.sell(run, 6))
  assert(Inventory.activate(run.player, 6))
  assert(run.player.power == 1)
  assert(Inventory.sell(run, 6) and run.coins == 18)
  assert(Inventory.add(run.player.inventory, "chainmail", 1))
  assert(Inventory.activate(run.player, 6))
  run.player.invulnerable = 0
  Combat.damage(run, run.player, 2)
  assert(math.abs(run.player.hp - 6.6) < 0.0001)
end)

test("shops charge only for delivered goods and chests open once", function()
  local run = Run.new()
  assert(not Inventory.buy(run, "chainmail"))
  run.coins = 20
  assert(Inventory.buy(run, "chainmail") and run.coins == 0)
  local chest = run.chests[1]
  run.player.x, run.player.y = chest.x, chest.y
  assert(Loot.interact(run) ~= "chest" and not chest.opened)
  for _, gate in ipairs(run.gates) do
    gate.open = true
  end
  assert(Loot.interact(run) == "chest" and chest.opened)
  assert(Inventory.count(run.player.inventory, chest.item) == 1)
  Loot.interact(run)
  assert(Inventory.count(run.player.inventory, chest.item) == 1)
end)

test("tonics expire and experience rewards are granted only once", function()
  local run = Run.new()
  Inventory.add(run.player.inventory, "ember_tonic", 1)
  Inventory.activate(run.player, 6)
  assert(run.player.power == 4)
  Combat.update_player(run, {}, open_space, 13)
  assert(run.player.power == 2)
  run.enemies[1].hp = 0
  Loot.update(run, 0)
  local xp = run.player.xp
  Loot.update(run, 0)
  assert(run.player.xp == xp and xp > 0)
end)

test("checkpoint preserves sparse inventory, gear, chests and encounter state", function()
  local run = Run.new()
  Inventory.activate(run.player, 2)
  Inventory.remove(run.player.inventory, 2)
  Inventory.add(run.player.inventory, "swift_boots", 1)
  Inventory.activate(run.player, 2)
  for _, gate in ipairs(run.gates) do
    gate.open = true
  end
  run.discovered["5:5"] = true
  run.journal = { "A test note" }
  run.player.mana = 3
  run.chests[1].opened = true
  run.enemies[1].hp = 1
  run.player.hp = 5
  run.enemies[4].hp = 0
  run.enemies[4].rewarded = true
  run.seals[1].claimed = true
  run.shrines[1].used = true
  Melee.start(run.player, "short_sword", 1, 0, 2)
  local encoded = Checkpoint.encode(run)
  local restored, message = Checkpoint.decode(encoded)
  assert(restored, message)
  assert(restored.player.inventory.equipment.charm == 2)
  assert(restored.enemies[1].hp == 1 and restored.chests[1].opened)
  assert(restored.player.hp == 5 and Inventory.stats(restored.player).speed == 5.6)
  assert(restored.seals[1].claimed and restored.shrines[1].used)
  assert(
    restored.player.mana == 3
      and restored.discovered["5:5"]
      and restored.journal[1] == "A test note"
  )
  assert(restored.gates[1].open)
  assert(restored.player.melee.profile == "short_sword")
  encoded.format_version = 999
  assert(not Checkpoint.decode(encoded))
  encoded = Checkpoint.encode(run)
  encoded.player.inventory.slots[1].item = "missing_item"
  assert(not Checkpoint.decode(encoded))
  encoded = Checkpoint.encode(run)
  encoded.player.mana = -1
  assert(not Checkpoint.decode(encoded))
end)

test("short and long swords have distinct reach, timing and single-hit sweeps", function()
  local function swing(profile, target_x)
    local actor = { id = "attacker", x = 0, y = 0, hp = 8, cooldown = 0 }
    local target = { id = "target", x = target_x, y = 0, hp = 20 }
    local hits = 0
    Melee.start(actor, profile, 1, 0, 3)
    local spec = Melee.profiles[profile]
    Melee.update(actor, { target }, open_space, spec.windup - 0.01, function()
      hits = hits + 1
    end)
    assert(hits == 0)
    for tick = 1, 180 do
      Melee.update(actor, { target }, open_space, 1 / 120, function()
        hits = hits + 1
      end)
    end
    return hits
  end
  assert(swing("short_sword", 1) == 1)
  assert(swing("short_sword", 2.3) == 0)
  assert(swing("long_sword", 2.3) == 1)
  assert(Melee.profiles.short_sword.recovery < Melee.profiles.long_sword.recovery)
  assert(Melee.profiles.short_sword.arc < Melee.profiles.long_sword.arc)
end)

test("melee locks its aim, respects walls and handles a large fixed step", function()
  local actor = { id = "attacker", x = 0, y = 0, hp = 8, cooldown = 0 }
  local front = { id = "front", x = 1, y = 0, hp = 8 }
  local back = { id = "back", x = -1, y = 0, hp = 8 }
  local hits = {}
  Melee.start(actor, "short_sword", 1, 0, 2)
  actor.aim_x = -1
  Melee.update(actor, { front, back }, open_space, 0.3, function(target)
    hits[target.id] = true
  end)
  assert(hits.front and not hits.back)
  actor.melee = nil
  Melee.start(actor, "long_sword", 1, 0, 4)
  local wall = {
    wall_distance = function()
      return 0.5
    end,
  }
  Melee.update(actor, { front }, wall, 1, function()
    error("Melee hit through a wall")
  end)
end)

test("swords never create projectiles and dodging cancels a committed swing", function()
  local run = Run.new()
  Combat.update_player(run, { attack = true, aim_x = 10, aim_y = 5.55 }, open_space, 1 / 60)
  assert(run.player.melee and #run.projectiles == 0)
  Combat.update_player(run, { dash = true, move_y = 1 }, open_space, 1 / 60)
  assert(not run.player.melee and run.player.dash_time > 0)
  run.player.dash_time = 0
  Combat.update_player(run, { swap_weapon = true }, open_space, 1 / 60)
  assert(Inventory.stats(run.player).weapon == "oak_staff")
  Combat.update_player(run, { swap_weapon = true }, open_space, 1 / 60)
  assert(Inventory.stats(run.player).weapon == "short_sword")
end)

test("equipping during any melee phase cancels the swing without resetting recovery", function()
  for _, elapsed in ipairs({ 0.05, 0.18, 0.3 }) do
    local run = Run.new()
    Melee.start(run.player, "short_sword", 1, 0, 2)
    Melee.update(run.player, {}, open_space, elapsed, function() end)
    run.player.cooldown = 0.48 - elapsed
    local remaining = run.player.cooldown
    assert(Inventory.activate(run.player, 4))
    assert(Inventory.stats(run.player).weapon == "oak_staff")
    assert(run.player.melee == nil and run.player.cooldown == remaining)
    Melee.update(run.player, { run.enemies[1] }, open_space, 1, function()
      error("Cancelled sword dealt damage")
    end)
    Combat.update_player(run, {}, open_space, 0.01)
    assert(run.player.cooldown > 0 and run.player.cooldown < remaining)
  end
end)

test("spike lanes warn before dealing one hit per cycle and shrines are single-use", function()
  local run = Run.new()
  local trap = run.floor.traps[1]
  run.player.x, run.player.y = trap.x, trap.y
  run.player.invulnerable = 0
  run.floor_time = 2.8 - trap.offset
  Encounters.update(run)
  assert(run.player.hp == 8)
  run.floor_time = 3.5 - trap.offset
  Encounters.update(run)
  assert(run.player.hp == 6)
  run.player.invulnerable = 0
  Encounters.update(run)
  assert(run.player.hp == 6)
  local shrine = run.shrines[1]
  run.player.x, run.player.y = shrine.x, shrine.y
  Encounters.interact(run)
  assert(shrine.used and run.player.hp == 8)
  run.player.hp = 3
  Encounters.interact(run)
  assert(run.player.hp == 3)
end)

test("all encounter objectives and treasures are reachable", function()
  for index = 1, 4 do
    local floor = Floors.create(index)
    local queue = { { floor.spawn[1], floor.spawn[2] } }
    local visited = {}
    local head = 1
    while queue[head] do
      local point = queue[head]
      head = head + 1
      local key = point[1] .. ":" .. point[2]
      if not visited[key] and Floors.walkable(floor, point[1], point[2]) then
        visited[key] = true
        for _, step in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) do
          queue[#queue + 1] = { point[1] + step[1], point[2] + step[2] }
        end
      end
    end
    for _, group in ipairs({ floor.seals, floor.shrines }) do
      for _, point in ipairs(group) do
        assert(visited[point.x .. ":" .. point.y], point.id)
      end
    end
    for _, chest in ipairs(floor.chests) do
      assert(visited[chest[1] .. ":" .. chest[2]])
    end
  end
end)

test("legacy checkpoints retain equipment and progression on the redesigned floor", function()
  local run = Run.new()
  local old = Checkpoint.encode(run)
  old.format_version = 1
  old.player.inventory.slots[1].item = "iron_wand"
  old.player.level, old.player.xp, old.coins = 3, 7, 42
  old.player.x, old.player.y = 0, 0
  local restored, message = Checkpoint.decode(old)
  assert(restored, message)
  assert(restored.player.inventory.slots[1].item == "short_sword")
  assert(restored.player.level == 3 and restored.player.xp == 7 and restored.coins == 42)
  assert(restored.player.x == restored.floor.spawn[1] and not restored.seals[1].claimed)
  assert(old.player.inventory.slots[1].item == "iron_wand")
end)

test("staff spam exhausts mana and waiting never refills it", function()
  local run = Run.new()
  run.enemies, run.breakables = {}, {}
  Inventory.activate(run.player, 4)
  for tick = 1, 600 do
    Combat.update_player(run, { attack = true, aim_x = 20, aim_y = 5.55 }, open_space, 1 / 60)
    Combat.update_projectiles(run, open_space, 1 / 60)
  end
  assert(run.shots == 4 and run.player.mana == 0)
  assert(#run.projectiles == 0)
  Combat.update_player(run, {}, open_space, 30)
  assert(run.player.mana == 0)
  assert(Inventory.restore_mana(run.player))
  assert(run.player.mana == 6 and Inventory.count(run.player.inventory, "mana_potion") == 0)
end)

test("one successful melee swing restores mana once even with multiple victims", function()
  local run = Run.new()
  run.player.mana = 0
  run.enemies = { Enemies.create("sentinel", 6, 5, 1), Enemies.create("sentinel", 6, 5.1, 2) }
  Combat.update_player(run, { attack = true, aim_x = 6, aim_y = 5.55 }, open_space, 1 / 60)
  for tick = 1, 20 do
    Combat.update_player(run, {}, open_space, 1 / 60)
  end
  assert(run.player.mana == 2)
  assert(run.enemies[1].hp < run.enemies[1].max_hp and run.enemies[2].hp < run.enemies[2].max_hp)
end)

test("spells have finite reach and frost and ember have distinct effects", function()
  local run = Run.new()
  run.enemies = { Enemies.create("sentinel", 6, 5, 1), Enemies.create("sentinel", 6, 6, 2) }
  Combat.fire(run, run.player, 1, 0, false, 8, 1, "frost")
  Combat.update_projectiles(run, open_space, 0.2)
  assert(run.enemies[1].slow_time == 2.5 and run.enemies[2].slow_time == 0)
  local first, second = run.enemies[1].hp, run.enemies[2].hp
  Combat.fire(run, run.player, 1, 0, false, 8, 3, "ember")
  Combat.update_projectiles(run, open_space, 0.2)
  assert(run.enemies[1].hp == first - 3 and run.enemies[2].hp == second - 3)
  run.enemies, run.breakables = {}, {}
  Combat.fire(run, run.player, 1, 0, false, 9, 1, "spark")
  local bolt = run.projectiles[1]
  local start = bolt.x
  Combat.update_projectiles(run, open_space, 10)
  assert(math.abs(bolt.x - start - 6) < 0.00001 and #run.projectiles == 0)
end)

test("a distant hit provokes pursuit beyond home distance and alerts nearby allies", function()
  local run = Run.new()
  local enemy = run.enemies[1]
  enemy.x, enemy.y, enemy.home_x, enemy.home_y = 20, 5, 20, 5
  Combat.damage(run, enemy, 1, "player")
  assert(enemy.awakened and enemy.alert_time == 10)
  assert(run.enemies[2].awakened)
  for tick = 1, 60 do
    Enemies.update(run, open_space, 1 / 60)
  end
  assert(enemy.x < 19 and enemy.awakened and enemy.last_seen_x == run.player.x)
end)

test("closed vault gates never prevent reaching the stairs", function()
  for index = 1, 4 do
    local run = Run.new()
    Run.enter_floor(run, index)
    local floor = run.floor
    local function blocked(x, y)
      if not Floors.walkable(floor, x, y) then
        return true
      end
      for _, gate in ipairs(run.gates) do
        if not gate.open and Exploration.gate_contains(gate, x, y) then
          return true
        end
      end
      return false
    end
    local queue, seen, head = { { floor.spawn[1], floor.spawn[2] } }, {}, 1
    while queue[head] do
      local point = queue[head]
      head = head + 1
      local key = Exploration.key(point[1], point[2])
      if not seen[key] and not blocked(point[1], point[2]) then
        seen[key] = true
        for _, d in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) do
          queue[#queue + 1] = { point[1] + d[1], point[2] + d[2] }
        end
      end
    end
    assert(seen[Exploration.key(floor.exit[1], floor.exit[2])], "Exit blocked on floor " .. index)
    if index < 4 then
      local chest = run.chests[1]
      assert(
        not seen[Exploration.key(chest.x, chest.y)],
        "Secret reward is reachable before discovery"
      )
    end
  end
end)

test("levers open linked routes and notes persist without duplicate entries", function()
  local run = Run.new()
  local lever = run.levers[1]
  run.player.x, run.player.y = lever.x, lever.y
  assert(Exploration.interact(run))
  assert(run.gates[1].open and run.gates[2].open and not run.gates[3].open)
  local clue = run.clues[1]
  run.player.x, run.player.y = clue.x, clue.y
  Exploration.interact(run)
  Exploration.interact(run)
  assert(#run.journal == 1 and clue.read)
end)

test("oblique walls do not occlude themselves and explored room rims remain known", function()
  assert(math.abs(Exploration.tile_entry_distance(0, 0, 1, 1) - math.sqrt(0.5)) < 0.000001)
  local run = Run.new()
  local geometry = {
    wall_distance = function(_, x, y, dx, dy, distance)
      if dx > 0 then
        local hit = (7.5 - x) / dx
        if hit >= 0 and hit <= distance then
          return hit
        end
      end
    end,
  }
  Exploration.update(run, geometry)
  assert(Exploration.visible(run, 8, 8), "Wall near face disappeared at an angle")
  assert(not Exploration.visible(run, 10, 10), "Vision leaked through a wall")
  assert(Exploration.discovered(run, 1, 3), "Entered room's wall rim was not revealed")
  run.player.x, run.player.y = 20, 6
  Exploration.update(run, open_space)
  assert(Exploration.discovered(run, 1, 3) and not Exploration.visible(run, 1, 3))
end)

test("secrets are not pre-discovered and containers drop resources only once", function()
  local run = Run.new()
  Exploration.update(run, open_space)
  assert(not Exploration.discovered(run, 3, 24))
  local prop = run.breakables[1]
  run.player.mana = 0
  Combat.damage(run, prop, 1, "player")
  Loot.update(run, 0)
  local count = #run.pickups
  Loot.update(run, 0)
  assert(#run.pickups == count and run.kills == 0)
  run.player.x, run.player.y = prop.x, prop.y
  Loot.update(run, 0)
  assert(run.player.mana == 2)
end)

print(string.format("Dungeon rules: %d passed, 0 failed", passed))
