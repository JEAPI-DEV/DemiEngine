local Config = {}

Config.start_gold = 150
Config.start_health = 20
Config.spawn = { 1, 9 }
Config.goal = { 16, 9 }
Config.save_slot = "isometric_base_builder"
Config.road = {
  { 1, 9 }, { 2, 9 }, { 3, 9 }, { 4, 9 },
  { 4, 8 }, { 4, 7 }, { 4, 6 }, { 4, 5 }, { 4, 4 }, { 4, 3 },
  { 5, 3 }, { 6, 3 },
  { 6, 4 }, { 6, 5 }, { 6, 6 }, { 6, 7 }, { 6, 8 }, { 6, 9 }, { 6, 10 }, { 6, 11 }, { 6, 12 }, { 6, 13 },
  { 7, 13 }, { 8, 13 }, { 9, 13 }, { 10, 13 }, { 11, 13 }, { 12, 13 }, { 13, 13 }, { 14, 13 },
  { 14, 12 }, { 14, 11 }, { 14, 10 }, { 14, 9 }, { 14, 8 }, { 14, 7 },
  { 15, 7 }, { 16, 7 }, { 16, 8 }, { 16, 9 },
}
Config.road_cells = {}
for _, cell in ipairs(Config.road) do
  Config.road_cells[cell[1] .. "," .. cell[2]] = true
end

Config.towers = {
  arrow = {
    label = "Arrow tower",
    cost = 50,
    damage = 15,
    range = 3.5,
    cooldown = 0.55,
    texture = "asset://units/archer_tower",
    size = { 1.55, 2.0 },
    projectile = {
      texture = "asset://projectiles/arrow",
      size = { 0.30, 0.50 },
      speed = 6.0,
    },
    upgrades = { max_level = 5, base_cost = 42, cost_growth = 1.45, power_multiplier = 0.34, range_bonus = 0.55, cooldown_reduction = 0.045, minimum_cooldown = 0.30, destroy_refund = 0.55 },
  },
  wizard = {
    label = "Wizard tower",
    cost = 90,
    damage = 38,
    range = 2.0,
    cooldown = 1.15,
    texture = "asset://units/wizard_tower",
    size = { 1.7, 1.45 },
    projectile = {
      texture = "asset://projectiles/fireball",
      size = { 0.38, 0.38 },
      speed = 3.5,
    },
    upgrades = { max_level = 5, base_cost = 70, cost_growth = 1.50, power_multiplier = 0.40, range_bonus = 0.42, cooldown_reduction = 0.035, minimum_cooldown = 0.62, destroy_refund = 0.55 },
  },
}

Config.enemy = {
  texture = "asset://units/raider",
  size = { 0.6, 0.85 },
  -- Every wave scales all enemies. Health grows faster than speed so late
  -- waves become harder without making movement unreadably fast.
  health_growth_per_wave = 0.26,
  speed_growth_per_wave = 0.025,
  reward_growth_every = 5,
  base_spawn_interval = 0.58,
  minimum_spawn_interval = 0.22,
  spawn_interval_reduction = 0.025,
  types = {
    raider = {
      label = "Raider",
      tint = { 1.0, 1.0, 1.0, 1.0 },
      health = 64,
      speed = 2.0,
      reward = 3,
      base_damage = 1,
      unlock_wave = 1,
      weight = 42,
    },
    scout = {
      label = "Scout",
      tint = { 0.48, 0.90, 1.0, 1.0 },
      health = 38,
      speed = 3.05,
      reward = 5,
      base_damage = 1,
      unlock_wave = 1,
      weight = 36,
    },
    brute = {
      label = "Brute",
      tint = { 1.0, 0.46, 0.38, 1.0 },
      health = 135,
      speed = 1.28,
      reward = 9,
      base_damage = 2,
      unlock_wave = 3,
      weight = 30,
    },
    champion = {
      label = "Champion",
      tint = { 0.82, 0.48, 1.0, 1.0 },
      health = 235,
      speed = 1.05,
      reward = 14,
      base_damage = 3,
      unlock_wave = 6,
      weight = 18,
    },
  },
}

return Config
