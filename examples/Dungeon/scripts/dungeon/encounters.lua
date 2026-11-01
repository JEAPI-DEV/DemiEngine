local Combat = require("dungeon.combat")
local Encounters = {}

function Encounters.create(run)
  run.seals, run.shrines, run.trap_hits = {}, {}, {}
  for index, source in ipairs(run.floor.seals) do
    run.seals[index] =
      { id = source.id, x = source.x, y = source.y, guard = source.guard, claimed = false }
  end
  for index, source in ipairs(run.floor.shrines) do
    run.shrines[index] = { id = source.id, x = source.x, y = source.y, used = false }
  end
end

function Encounters.unlocked(run)
  for _, seal in ipairs(run.seals) do
    if not seal.claimed then
      return false
    end
  end
  return true
end

function Encounters.guarded(run, seal)
  for _, enemy in ipairs(run.enemies) do
    if enemy.id == seal.guard then
      return enemy.hp > 0
    end
  end
  return false
end

function Encounters.trap_phase(time, trap)
  local phase = (time + trap.offset) % 4
  return phase < 2.5 and "safe" or (phase < 3.3 and "warning" or "active")
end

function Encounters.update(run)
  for index, trap in ipairs(run.floor.traps) do
    if Encounters.trap_phase(run.floor_time, trap) == "active" then
      local cycle = math.floor((run.floor_time + trap.offset) / 4)
      local actors = { run.player }
      for _, enemy in ipairs(run.enemies) do
        actors[#actors + 1] = enemy
      end
      for _, actor in ipairs(actors) do
        local key = index .. ":" .. actor.id
        if
          actor.hp > 0
          and math.abs(actor.x - trap.x) < trap.width / 2
          and math.abs(actor.y - trap.y) < trap.height / 2
          and run.trap_hits[key] ~= cycle
        then
          run.trap_hits[key] = cycle
          Combat.damage(run, actor, 2, "environment")
        end
      end
    end
  end
end

function Encounters.interact(run)
  for _, seal in ipairs(run.seals) do
    if not seal.claimed and (seal.x - run.player.x) ^ 2 + (seal.y - run.player.y) ^ 2 < 1.5 ^ 2 then
      seal.claimed = true
      run.coins = run.coins + 5
      run.player.mana = math.min(run.player.max_mana, run.player.mana + 2)
      run.message = "Optional sigil claimed: +5 souls and +2 mana. The stairs never required it."
      return true
    end
  end
  for _, shrine in ipairs(run.shrines) do
    if
      not shrine.used and (shrine.x - run.player.x) ^ 2 + (shrine.y - run.player.y) ^ 2 < 1.5 ^ 2
    then
      if run.player.hp >= run.player.max_hp and run.player.mana >= run.player.max_mana then
        run.message = "The shrine waits here until you need healing."
      else
        shrine.used = true
        run.player.hp = run.player.max_hp
        run.player.mana = run.player.max_mana
        run.message = "The shrine restores your vitality. Its blessing is spent."
      end
      return true
    end
  end
  return false
end

return Encounters
