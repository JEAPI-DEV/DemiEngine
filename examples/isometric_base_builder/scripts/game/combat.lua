local TowerStats = require("game.tower_stats")
local Targeting = require("game.targeting")
local Combat = {}
function Combat.update(state, config, projectiles, dt)
  for _, tower in pairs(state.towers) do
    tower.cooldown = math.max(0, tower.cooldown - dt)
    if tower.cooldown <= 0 then
      local definition = config.towers[tower.kind]
      local stats = TowerStats.get(tower, definition)
      local target = Targeting.choose(tower, state.enemies, stats.range * stats.range)
      if target then projectiles.spawn(tower, target, definition, stats) tower.cooldown = stats.cooldown end
    end
  end
  for id in pairs(projectiles.update(dt)) do
    local enemy = state.enemies[id]
    if enemy then
      Entity.destroy(id) state.enemies[id] = nil
      local reward = enemy.reward or 0 state.gold = state.gold + reward
      state.status = (enemy.label or "Enemy") .. " defeated: +" .. tostring(reward) .. " gold."
    end
  end
end
return Combat
