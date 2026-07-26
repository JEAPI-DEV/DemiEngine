local Config = {
  port = 39420,
  player_entity = "ent_player",
  arena_half_width = 13.25,
  arena_half_height = 7.25,
  player_speed = 6.5,
  shot_range = 24.0,
  shot_radius = 0.65,
  shot_damage = 34,
  shot_cooldown = 0.28,
  respawn_invulnerability = 0.8,
  join_timeout = 8.0,
}

Config.spawns = {
  {-10.5, -5.5},
  {10.5, 5.5},
  {-10.5, 5.5},
  {10.5, -5.5},
  {-6.5, 0.0},
  {6.5, 0.0},
  {0.0, 5.5},
  {0.0, -5.5},
}

Config.colors = {
  {0.20, 0.90, 0.75, 1.0},
  {1.00, 0.45, 0.32, 1.0},
  {0.44, 0.65, 1.00, 1.0},
  {0.96, 0.38, 0.82, 1.0},
  {1.00, 0.82, 0.28, 1.0},
  {0.58, 0.92, 0.38, 1.0},
  {0.72, 0.48, 1.00, 1.0},
  {0.28, 0.88, 1.00, 1.0},
}

local function stable_index(id, count)
  local value = 0
  for index = 1, #id do
    value = (value * 33 + id:byte(index)) % 65521
  end
  return (value % count) + 1
end

function Config.spawn_for(id, deaths)
  local index = stable_index(id .. tostring(deaths or 0), #Config.spawns)
  return Config.spawns[index][1], Config.spawns[index][2]
end

function Config.color_for(id)
  return Config.colors[stable_index(id, #Config.colors)]
end

return Config
