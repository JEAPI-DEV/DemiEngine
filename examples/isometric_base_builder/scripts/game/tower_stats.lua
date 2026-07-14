local TowerStats = {}

function TowerStats.get(tower, definition)
  local power_level = tower.power_level or 0
  local range_level = tower.range_level or 0
  local upgrades = power_level + range_level
  return {
    level = 1 + upgrades,
    damage = math.floor(definition.damage * (1 + power_level * definition.upgrades.power_multiplier) + 0.5),
    range = definition.range + range_level * definition.upgrades.range_bonus,
    cooldown = math.max(definition.upgrades.minimum_cooldown,
      definition.cooldown * (1 - power_level * definition.upgrades.cooldown_reduction)),
  }
end

function TowerStats.upgrade_cost(tower, definition)
  local level = 1 + (tower.power_level or 0) + (tower.range_level or 0)
  return math.floor(definition.upgrades.base_cost *
    (definition.upgrades.cost_growth ^ (level - 1)) + 0.5)
end

function TowerStats.invested_gold(tower, definition)
  local total = definition.cost
  local simulated = { power_level = 0, range_level = 0 }
  local power = tower.power_level or 0
  local range = tower.range_level or 0
  for _ = 1, power do
    total = total + TowerStats.upgrade_cost(simulated, definition)
    simulated.power_level = simulated.power_level + 1
  end
  for _ = 1, range do
    total = total + TowerStats.upgrade_cost(simulated, definition)
    simulated.range_level = simulated.range_level + 1
  end
  return total
end

return TowerStats
