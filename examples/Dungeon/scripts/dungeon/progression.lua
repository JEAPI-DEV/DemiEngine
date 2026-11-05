local Inventory = require("dungeon.inventory")
local Progression = {}
Progression.paths = {
  vigor = { name = "VIGOR", description = "+2 vitality per rank. Rank 2: +1 armor." },
  blade = {
    name = "BLADE",
    description = "+1 melee power per rank. Rank 2: faster dodge recovery.",
  },
  arcana = { name = "ARCANA", description = "+2 mana per rank. Rank 2: spells cost 1 less mana." },
}

function Progression.threshold(player)
  return 20 + (player.level - 1) * 8
end

function Progression.award(run, amount)
  local player = run.player
  player.xp = player.xp + amount
  while player.xp >= Progression.threshold(player) do
    player.xp = player.xp - Progression.threshold(player)
    player.level = player.level + 1
    player.skill_points = (player.skill_points or 0) + 1
    run.message = "Level " .. player.level .. "! Press C to choose a talent."
  end
end

function Progression.spend(run, path)
  local player = run.player
  if not Progression.paths[path] or (player.skill_points or 0) < 1 then
    return false
  end
  player.talents = player.talents or {}
  local rank = player.talents[path] or 0
  if rank >= 3 then
    return false
  end
  player.talents[path] = rank + 1
  player.skill_points = player.skill_points - 1
  Inventory.refresh(player)
  if path == "vigor" then
    player.hp = math.min(player.max_hp, player.hp + 2)
  elseif path == "arcana" then
    player.mana = math.min(player.max_mana, player.mana + 2)
  end
  run.message = Progression.paths[path].name .. " rank " .. (rank + 1) .. " learned."
  return true
end

return Progression
