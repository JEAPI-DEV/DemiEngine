local State = {}

function State.new(config)
  return {
    gold = config.start_gold,
    base_health = config.start_health,
    wave = 0,
    status = "Choose a tower, then click a free tile.",
    build_kind = nil,
    selected_id = nil,
    towers = {},
    enemies = {},
    next_tower_id = 1,
    next_enemy_id = 1,
    wave_active = false,
    spawn_remaining = 0,
    spawn_timer = 0,
    left_mouse_was_down = false,
  }
end

return State
