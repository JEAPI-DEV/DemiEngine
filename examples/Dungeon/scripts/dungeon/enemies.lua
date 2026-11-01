local Combat = require("dungeon.combat")
local Body = require("dungeon.body")
local Melee = require("dungeon.melee")
local Enemies = {}

local types = {
  crawler = {
    hp = 4,
    speed = 1.7,
    radius = 0.3,
    notice = 4,
    range = 2.1,
    melee = "bite",
    damage = 1,
  },
  skirmisher = {
    hp = 6,
    speed = 2.5,
    radius = 0.3,
    notice = 6,
    range = 1.8,
    melee = "duelist_sword",
    damage = 1,
  },
  sentinel = {
    hp = 11,
    speed = 1.4,
    radius = 0.4,
    notice = 7,
    range = 2.9,
    melee = "guard_sword",
    damage = 3,
  },
  acolyte = { hp = 5, speed = 1.8, radius = 0.3, notice = 6.5, range = 7.5 },
  boss = { hp = 130, speed = 1.4, radius = 0.8, notice = 6.5, range = 12 },
}

function Enemies.create(kind, x, y, index)
  local spec = assert(types[kind])
  return {
    id = "enemy_" .. index,
    kind = kind,
    x = x,
    y = y,
    home_x = x,
    home_y = y,
    hp = spec.hp,
    max_hp = spec.hp,
    radius = spec.radius,
    alert_time = 0,
    charge_windup = 0,
    charge_time = 0,
    slow_time = 0,
    last_seen_x = x,
    last_seen_y = y,
    cooldown = 0.6 + index * 0.08,
    windup = 0,
    flash = 0,
    path_timer = 0,
    pattern = kind == "boss" and 0 or index % 2,
    awakened = false,
    behavior = "patrol",
  }
end

local function walk_to(enemy, x, y, geometry, speed, dt)
  if enemy.path_timer <= 0 then
    enemy.path = geometry:path(enemy.x, enemy.y, x, y)
    enemy.path_index = 2
    enemy.path_timer = 0.35
  end
  local target = enemy.path and enemy.path[enemy.path_index]
  local dx, dy, distance = Combat.direction(
    (target and target.world_x or x) - enemy.x,
    (target and target.world_y or y) - enemy.y
  )
  if target and distance < 0.18 then
    enemy.path_index = enemy.path_index + 1
  end
  if distance > 0.1 then
    enemy.aim_x, enemy.aim_y = dx, dy
    geometry:move(enemy, dx * speed * dt, dy * speed * dt)
  end
end

local function aim(enemy, player)
  local x, y = Body.muzzle(enemy)
  local tx, ty = Body.muzzle(player)
  enemy.aim_x, enemy.aim_y = Combat.direction(tx - x, ty - y)
end

local function cast(run, enemy)
  if enemy.kind == "acolyte" then
    Combat.fire(run, enemy, enemy.aim_x, enemy.aim_y, true, 5, 1)
  else
    local enraged = enemy.hp <= enemy.max_hp / 2
    if enemy.pattern % 2 == 0 then
      local count = enraged and 16 or 12
      for index = 1, count do
        local angle = index * math.pi * 2 / count + enemy.pattern * 0.19
        Combat.fire(run, enemy, math.cos(angle), math.sin(angle), true, enraged and 4.2 or 3.4, 1)
      end
    else
      for index = -2, 2 do
        local angle = math.atan(enemy.aim_y, enemy.aim_x) + index * 0.16
        Combat.fire(run, enemy, math.cos(angle), math.sin(angle), true, 6, 1)
      end
    end
    enemy.pattern = enemy.pattern + 1
    run.events[#run.events + 1] = "boss"
  end
end

function Enemies.update(run, geometry, dt)
  for _, enemy in ipairs(run.enemies) do
    if enemy.hp > 0 then
      local spec = types[enemy.kind]
      enemy.alert_time = math.max(0, enemy.alert_time - dt)
      enemy.slow_time = math.max(0, enemy.slow_time - dt)
      enemy.charge_time = math.max(0, enemy.charge_time - dt)
      local speed = spec.speed * (enemy.slow_time > 0 and 0.5 or 1)
      if enemy.charge_time > 0 then
        speed = speed * 2
      end
      enemy.flash = math.max(0, enemy.flash - dt)
      enemy.cooldown = math.max(0, enemy.cooldown - dt)
      enemy.path_timer = enemy.path_timer - dt
      local dx, dy, distance = Combat.direction(run.player.x - enemy.x, run.player.y - enemy.y)
      local visible = not geometry:wall_distance(enemy.x, enemy.y, dx, dy, distance)
      local _, _, home_distance =
        Combat.direction(run.player.x - enemy.home_x, run.player.y - enemy.home_y)
      if visible and (distance < spec.notice or enemy.alert_time > 0) then
        enemy.awakened = true
        enemy.alert_time = 8
        enemy.last_seen_x, enemy.last_seen_y = run.player.x, run.player.y
      elseif enemy.alert_time > 0 then
        enemy.awakened = true
      elseif home_distance > 11 or not visible then
        enemy.awakened = false
        enemy.provoked = false
      end
      if enemy.charge_windup > 0 and not enemy.melee then
        enemy.behavior = "charge_warning"
        enemy.charge_windup = math.max(0, enemy.charge_windup - dt)
        if enemy.charge_windup == 0 then
          enemy.charge_time = 1.1
        end
      elseif enemy.melee then
        enemy.behavior = enemy.melee.phase
        if enemy.melee.phase == "recovery" then
          geometry:move(enemy, -dx * 0.8 * dt, -dy * 0.8 * dt)
        elseif enemy.kind == "crawler" and enemy.melee.phase == "active" then
          local angle = enemy.melee.angle
          geometry:move(enemy, math.cos(angle) * 6 * dt, math.sin(angle) * 6 * dt)
        end
        Melee.update(enemy, { run.player }, geometry, dt, function(target, amount)
          Combat.damage(run, target, amount)
        end)
      elseif enemy.windup > 0 then
        enemy.behavior = "casting"
        enemy.windup = enemy.windup - dt
        if enemy.windup <= 0 then
          cast(run, enemy)
          enemy.cooldown = enemy.kind == "boss" and 1.6 or 1.5
        end
      elseif not enemy.awakened then
        enemy.behavior = "patrol"
        if enemy.kind == "crawler" or enemy.kind == "sentinel" then
          walk_to(enemy, enemy.home_x, enemy.home_y, geometry, speed, dt)
        else
          local patrol = math.floor(run.floor_time / 3 + enemy.home_x) % 2 == 0 and -1 or 1
          walk_to(enemy, enemy.home_x + patrol, enemy.home_y, geometry, speed * 0.5, dt)
        end
      elseif enemy.kind == "acolyte" and distance < 3.5 and visible then
        enemy.behavior = "retreat"
        geometry:move(enemy, -dx * speed * dt, -dy * speed * dt)
      elseif
        enemy.cooldown == 0
        and visible
        and distance < (enemy.kind == "boss" and 4 or spec.range)
      then
        aim(enemy, run.player)
        if spec.melee or enemy.kind == "boss" then
          Melee.start(
            enemy,
            spec.melee or "crown_sword",
            enemy.aim_x,
            enemy.aim_y,
            spec.damage or 3
          )
          enemy.behavior = "windup"
        else
          enemy.windup = 0.7
        end
      elseif enemy.kind == "boss" and enemy.cooldown == 0 and visible then
        aim(enemy, run.player)
        enemy.windup = 0.95
      elseif enemy.kind == "skirmisher" and distance < 3.2 and enemy.cooldown > 0 then
        enemy.behavior = "flank"
        local side = enemy.pattern == 0 and 1 or -1
        geometry:move(enemy, -dy * side * speed * dt, dx * side * speed * dt)
      elseif (enemy.kind ~= "acolyte" and enemy.kind ~= "boss") or distance > 5 or not visible then
        enemy.behavior = "approach"
        walk_to(
          enemy,
          visible and run.player.x or enemy.last_seen_x,
          visible and run.player.y or enemy.last_seen_y,
          geometry,
          speed,
          dt
        )
      else
        enemy.behavior = "reposition"
        local side = math.floor(run.floor_time / 2) % 2 == 0 and 1 or -1
        geometry:move(enemy, -dy * side * speed * 0.4 * dt, dx * side * speed * 0.4 * dt)
      end
    end
  end
end

return Enemies
