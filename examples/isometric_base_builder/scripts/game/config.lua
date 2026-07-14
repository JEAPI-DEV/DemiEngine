local Config = {}

Config.start_gold = 150
Config.start_health = 20
Config.spawn = { 1, 9 }
Config.goal = { 16, 9 }
Config.save_slot = "isometric_base_builder"
Config.road = {
  { 1, 9 }, { 2, 9 }, { 3, 9 }, { 4, 9 }, { 5, 9 },
  { 5, 8 }, { 5, 7 }, { 5, 6 }, { 5, 5 },
  { 6, 5 }, { 7, 5 }, { 8, 5 }, { 9, 5 }, { 10, 5 }, { 11, 5 }, { 12, 5 },
  { 12, 6 }, { 12, 7 }, { 12, 8 }, { 12, 9 },
  { 13, 9 }, { 14, 9 }, { 15, 9 }, { 16, 9 },
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
  },
  wizard = {
    label = "Wizard tower",
    cost = 90,
    damage = 38,
    range = 2,
    cooldown = 1.15,
    texture = "asset://units/wizard_tower",
    size = { 1.7, 1.45 },
    projectile = {
      texture = "asset://projectiles/fireball",
      size = { 0.38, 0.38 },
      speed = 3.5,
    },
  },
}

Config.enemy = {
  texture = "asset://units/raider",
  size = { 0.6, 0.85 },
  -- Every wave scales all enemies. Health grows faster than speed so late
  -- waves become harder without making movement unreadably fast.
  health_growth_per_wave = 0.2,
  speed_growth_per_wave = 0.02,
  reward_growth_every = 1.15,
  base_spawn_interval = 0.7,
  minimum_spawn_interval = 0.2,
  spawn_interval_reduction = 0.02,
  types = {
    raider = {
      label = "Raider",
      tint = { 1.0, 1.0, 1.0, 1.0 },
      health = 64,
      speed = 2.0,
      reward = 3,
      base_damage = 1,
      unlock_wave = 1,
      weight = 55,
    },
    scout = {
      label = "Scout",
      tint = { 0.48, 0.90, 1.0, 1.0 },
      health = 38,
      speed = 3.05,
      reward = 5,
      base_damage = 1,
      unlock_wave = 2,
      weight = 32,
    },
    brute = {
      label = "Brute",
      tint = { 1.0, 0.46, 0.38, 1.0 },
      health = 135,
      speed = 1.28,
      reward = 9,
      base_damage = 2,
      unlock_wave = 4,
      weight = 22,
    },
    champion = {
      label = "Champion",
      tint = { 0.82, 0.48, 1.0, 1.0 },
      health = 235,
      speed = 1.05,
      reward = 14,
      base_damage = 3,
      unlock_wave = 8,
      weight = 10,
    },
  },
}

return Config