local TowerStats = require("game.tower_stats")
local Combat = {}
local function distance_squared(a,b) local dx,dy=a.x-b.x,a.y-b.y return dx*dx+dy*dy end
function Combat.update(state, config, projectiles, dt)
  for _, tower in pairs(state.towers) do
    tower.cooldown = math.max(0, tower.cooldown - dt)
    if tower.cooldown <= 0 then
      local definition = config.towers[tower.kind]
      local stats = TowerStats.get(tower, definition)
      local target, target_distance = nil, stats.range * stats.range
      for _, enemy in pairs(state.enemies) do
        local distance = distance_squared(tower, enemy)
        if distance <= target_distance then target, target_distance = enemy, distance end
      end
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
