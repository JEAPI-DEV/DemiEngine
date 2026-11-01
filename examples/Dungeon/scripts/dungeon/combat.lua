-- Combat rules consume narrow movement/query operations, never renderer state.
local Body = require("dungeon.body")
local Inventory = require("dungeon.inventory")
local Melee = require("dungeon.melee")
local Items = require("dungeon.items")
local Magic = require("dungeon.magic")
local Threat = require("dungeon.threat")
local Combat = {}

function Combat.direction(x, y)
  local length = math.sqrt(x * x + y * y)
  if length < 0.0001 then
    return 0, 1, 0
  end
  return x / length, y / length, length
end

function Combat.damage(run, actor, amount, source)
  if actor.hp <= 0 or (actor.invulnerable or 0) > 0 then
    return false
  end
  if actor == run.player then
    amount = amount * (1 - Inventory.stats(actor).defense * 0.15)
  end
  if actor ~= run.player and actor.kind ~= "urn" and source ~= "environment" then
    Threat.provoke(run, actor)
  end
  actor.hp = math.max(0, actor.hp - amount)
  actor.flash = 0.12
  if actor == run.player then
    actor.invulnerable = 0.7
    run.damage_taken = run.damage_taken + amount
    run.events[#run.events + 1] = "hurt"
    if actor.hp == 0 then
      run.phase = "dead"
    end
  else
    run.events[#run.events + 1] = "hit"
    if actor.hp == 0 and actor.kind ~= "urn" then
      run.kills = run.kills + 1
      run.coins = run.coins + (actor.kind == "boss" and 25 or 3)
      run.events[#run.events + 1] = "kill"
    end
  end
  return true
end

function Combat.fire(run, owner, dx, dy, hostile, speed, damage, spell_id)
  dx, dy = Combat.direction(dx, dy)
  local muzzle_x, muzzle_y = Body.muzzle(owner)
  run.next_projectile = run.next_projectile + 1
  run.projectiles[#run.projectiles + 1] = {
    id = "bolt_" .. run.next_projectile,
    x = muzzle_x,
    y = muzzle_y,
    dx = dx,
    dy = dy,
    speed = speed,
    damage = damage,
    hostile = hostile,
    life = spell_id and Magic.profiles[spell_id].range / speed or 2.4,
    spell = spell_id,
  }
end

function Combat.update_projectiles(run, geometry, dt)
  for index = #run.projectiles, 1, -1 do
    local bolt = run.projectiles[index]
    bolt.life = bolt.life - dt
    local distance = bolt.speed * math.min(dt, math.max(0, bolt.life + dt))
    local wall = geometry:wall_distance(bolt.x, bolt.y, bolt.dx, bolt.dy, distance)
    local limit = wall or distance
    local victim, closest
    local targets = bolt.hostile and { run.player } or {}
    if not bolt.hostile then
      for _, enemy in ipairs(run.enemies) do
        targets[#targets + 1] = enemy
      end
      for _, prop in ipairs(run.breakables or {}) do
        targets[#targets + 1] = prop
      end
    end
    for _, target in ipairs(targets) do
      if target.hp > 0 then
        local hit = Body.intersection(bolt, target, limit)
        if hit and (not closest or hit < closest) then
          victim, closest = target, hit
        end
      end
    end
    if victim then
      Combat.damage(run, victim, bolt.damage, bolt.hostile and "enemy" or "player")
      local spell = bolt.spell and Magic.profiles[bolt.spell]
      if spell and spell.slow and victim.hp > 0 then
        victim.slow_time = spell.slow
      end
      if spell and spell.splash then
        for _, target in ipairs(targets) do
          local dx, dy, distance = Combat.direction(target.x - victim.x, target.y - victim.y)
          if
            target ~= victim
            and target.hp > 0
            and distance <= spell.splash
            and not geometry:wall_distance(victim.x, victim.y, dx, dy, distance)
          then
            Combat.damage(run, target, bolt.damage, "player")
          end
        end
      end
    end
    bolt.x = bolt.x + bolt.dx * limit
    bolt.y = bolt.y + bolt.dy * limit
    if victim or wall or bolt.life <= 0 then
      table.remove(run.projectiles, index)
    end
  end
end

function Combat.update_player(run, action, geometry, dt)
  local player = run.player
  if action.swap_weapon and not player.melee and player.dash_time == 0 then
    run.message = Inventory.cycle_weapon(player)
  end
  player.cast_slow = math.max(0, (player.cast_slow or 0) - dt)
  if action.restore_mana then
    local _, message = Inventory.restore_mana(player)
    run.message = message
  end
  player.buff_time = math.max(0, player.buff_time - dt)
  local stats = Inventory.refresh(player)
  if action.heal then
    local used, message = Inventory.heal(player)
    run.message = message
    if used then
      run.events[#run.events + 1] = "heal"
    end
  end
  player.cooldown = math.max(0, player.cooldown - dt)
  player.dash_cooldown = math.max(0, player.dash_cooldown - dt)
  player.invulnerable = math.max(0, player.invulnerable - dt)
  player.flash = math.max(0, player.flash - dt)
  local dx, dy, magnitude = Combat.direction(action.move_x or 0, action.move_y or 0)
  if magnitude == 0 then
    dx, dy = 0, 0
  end
  if action.aim_x and action.aim_y then
    local muzzle_x, muzzle_y = Body.muzzle(player)
    local ax, ay, length = Combat.direction(action.aim_x - muzzle_x, action.aim_y - muzzle_y)
    if length > 0 then
      player.aim_x, player.aim_y = ax, ay
    end
  elseif magnitude > 0 then
    player.aim_x, player.aim_y = dx, dy
  end
  if action.dash and player.dash_cooldown == 0 then
    player.melee = nil
    player.dash_time = 0.18
    player.dash_cooldown = stats.dash_delay
    player.invulnerable = 0.24
    player.dash_x = magnitude > 0 and dx or player.aim_x
    player.dash_y = magnitude > 0 and dy or player.aim_y
    run.events[#run.events + 1] = "dash"
  end
  local speed = stats.speed * (player.cast_slow > 0 and 0.55 or 1)
  if player.dash_time > 0 then
    player.dash_time = math.max(0, player.dash_time - dt)
    dx, dy, speed = player.dash_x, player.dash_y, 13
  end
  if player.melee then
    speed = speed * (player.melee.phase == "recovery" and 0.65 or 0.4)
  end
  geometry:move(player, dx * speed * dt, dy * speed * dt)
  if action.attack and player.cooldown == 0 and player.dash_time == 0 then
    local item = Items[stats.weapon]
    if not item or item.melee then
      Melee.start(
        player,
        item and item.melee or "unarmed",
        player.aim_x,
        player.aim_y,
        player.power
      )
      run.events[#run.events + 1] = "dash"
    else
      local spell = Magic.profiles[item.spell]
      if spell and Magic.spend(player, item.spell) then
        local damage = spell.damage + math.floor((player.power - 1) * 0.5)
        Combat.fire(run, player, player.aim_x, player.aim_y, false, spell.speed, damage, item.spell)
        player.cooldown = spell.interval
        player.cast_slow = 0.25
        run.shots = run.shots + 1
        run.events[#run.events + 1] = "shoot"
      else
        player.cooldown = 0.2
        run.message = "Out of mana. X to switch weapons; melee hits restore 2 mana. T uses a flask."
      end
    end
  end
  local targets = {}
  for _, enemy in ipairs(run.enemies) do
    targets[#targets + 1] = enemy
  end
  for _, prop in ipairs(run.breakables or {}) do
    targets[#targets + 1] = prop
  end
  Melee.update(player, targets, geometry, dt, function(target, amount)
    local hit = Combat.damage(run, target, amount, "player")
    if hit and target.kind ~= "urn" and not player.melee.mana_restored then
      Magic.restore(player, 2)
      player.melee.mana_restored = true
    end
  end)
end

return Combat
