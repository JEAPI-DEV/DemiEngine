-- Authored, skippable room entrances. The cinematic owns movement only while
-- it is active; normal combat and navigation resume together afterwards.
local Director = {}

function Director.prepare(run)
  for _, encounter in ipairs(run.floor.ambushes or {}) do
    for _, entry in ipairs(encounter.actors) do
      for _, enemy in ipairs(run.enemies) do
        if enemy.id == entry.id then
          enemy.dormant = true
        end
      end
    end
  end
end

local function actor(run, id)
  for _, enemy in ipairs(run.enemies) do
    if enemy.id == id then
      return enemy
    end
  end
end

function Director.update(run, action, geometry, dt)
  if not run.cinematic then
    for _, encounter in ipairs(run.floor.ambushes or {}) do
      local bounds = encounter.trigger
      local guard = actor(run, encounter.actors[1].id)
      local entered = run.player.x >= bounds[1]
        and run.player.x <= bounds[3]
        and run.player.y >= bounds[2]
        and run.player.y <= bounds[4]
      if entered and guard and guard.dormant then
        run.cinematic = { encounter = encounter, time = 0 }
        run.player.melee = nil
        run.projectiles = {}
        break
      end
    end
  end
  local sequence = run.cinematic
  if not sequence then
    return false
  end
  sequence.time = sequence.time + dt
  run.player.invulnerable = 1
  local finished = sequence.time >= 4.2 or action.interact
  for index, entry in ipairs(sequence.encounter.actors) do
    local enemy = actor(run, entry.id)
    if enemy then
      local start = entry.from
      local destination = entry.to
      local side = index == 1 and -1.25 or 1.25
      local progress = math.max(0, math.min(1, (sequence.time - 0.7) / 3))
      -- Step around the plinth, then descend into view. Both skip and natural
      -- completion use the same authored end position and grace period.
      local x, y
      if progress < 0.25 then
        x, y = start[1] + side * progress * 4, start[2]
      elseif progress < 0.85 then
        x = start[1] + side
        y = start[2] + (destination[2] - start[2]) * (progress - 0.25) / 0.6
      else
        x = start[1] + side * (1 - (progress - 0.85) / 0.15)
        y = destination[2]
      end
      if finished then
        x, y = destination[1], destination[2]
        -- Skipping must follow the same clear route around the collider. A
        -- single sweep to the final pose would stop against the plinth.
        geometry:move(enemy, start[1] + side - enemy.x, 0)
        geometry:move(enemy, 0, y - enemy.y)
        enemy.dormant = false
        enemy.alert_time = 10
        enemy.cooldown = 1
        enemy.last_seen_x, enemy.last_seen_y = run.player.x, run.player.y
      end
      geometry:move(enemy, x - enemy.x, y - enemy.y)
      enemy.aim_x, enemy.aim_y = 0, -1
    end
  end
  if finished then
    run.message = "The oathbound awaken. Fight, slip past, or take the eastern passage."
    run.cinematic = nil
  end
  return true
end

return Director
