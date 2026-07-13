local Combat = {}

local function distance_squared(a, b)
  local dx, dy = a.x - b.x, a.y - b.y
  return dx * dx + dy * dy
end

function Combat.update(state, config, dt)
  local defeated = {}
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
        target.health = target.health - definition.damage
        tower.cooldown = definition.cooldown
        Entity.set_sprite_color(target.id, 1.0, 0.82, 0.32, 1.0)
        if target.health <= 0 then defeated[#defeated + 1] = target.id end
      end
    end
  end
  for _, id in ipairs(defeated) do
    if state.enemies[id] then
      Entity.destroy(id)
      state.enemies[id] = nil
      state.gold = state.gold + config.enemy.reward
      state.status = "Raider defeated: +" .. tostring(config.enemy.reward) .. " gold."
    end
  end
end

return Combat
