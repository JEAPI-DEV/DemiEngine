local Config = {}

Config.start_gold = 240
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
    damage = 16,
    range = 3.4,
    cooldown = 0.55,
    shape = "triangle",
    color = { 0.30, 0.88, 0.70, 1.0 },
  },
  cannon = {
    label = "Cannon tower",
    cost = 90,
    damage = 38,
    range = 2.7,
    cooldown = 1.15,
    shape = "circle",
    color = { 0.72, 0.42, 0.88, 1.0 },
  },
}

Config.enemy = {
  base_health = 52,
  health_per_wave = 12,
  base_speed = 2.0,
  reward = 14,
  color = { 0.92, 0.30, 0.34, 1.0 },
}

return Config
