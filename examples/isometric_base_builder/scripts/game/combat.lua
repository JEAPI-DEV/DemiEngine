local Combat = {}

local function distance_squared(a, b)
  local dx, dy = a.x - b.x, a.y - b.y
  return dx * dx + dy * dy
end

function Combat.update(state, config, projectiles, dt)
  for _, tower in pairs(state.towers) do
    tower.cooldown = math.max(0, tower.cooldown - dt)
    if tower.cooldown <= 0 then
      local definition = config.towers[tower.kind]
      local target, target_distance = nil, definition.range * definition.range
      for _, enemy in pairs(state.enemies) do
        local distance = distance_squared(tower, enemy)
        if distance <= target_distance then
          target, target_distance = enemy, distance
        end
      end
      if target then
        projectiles.spawn(tower, target, definition)
        tower.cooldown = definition.cooldown
      end
    end
  end

  for id in pairs(projectiles.update(dt)) do
    if state.enemies[id] then
      Entity.destroy(id)
      state.enemies[id] = nil
      state.gold = state.gold + config.enemy.reward
      state.status = "Raider defeated: +" .. tostring(config.enemy.reward) .. " gold."
    end
  end
end

return Combat
