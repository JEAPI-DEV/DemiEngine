local Waves = {}

function Waves.new(state, config)
  local self = {}

  local function available_enemy_types()
    local available = {}
    for kind, definition in pairs(config.enemy.types) do
      if state.wave >= definition.unlock_wave then
        available[#available + 1] = { kind = kind, definition = definition }
      end
    end
    table.sort(available, function(a, b)
      if a.definition.unlock_wave == b.definition.unlock_wave then
        return a.kind < b.kind
      end
      return a.definition.unlock_wave < b.definition.unlock_wave
    end)
    return available
  end

  local function choose_enemy_type()
    local available = available_enemy_types()
    local total_weight = 0
    for _, entry in ipairs(available) do
      local weight = entry.definition.weight
      -- Newly unlocked enemies get a temporary introduction boost.
      if state.wave == entry.definition.unlock_wave then weight = weight * 2 end
      entry.current_weight = weight
      total_weight = total_weight + weight
    end
    local roll = math.random() * total_weight
    local accumulated = 0
    for _, entry in ipairs(available) do
      accumulated = accumulated + entry.current_weight
      if roll <= accumulated then return entry.kind, entry.definition end
    end
    local fallback = available[#available]
    return fallback.kind, fallback.definition
  end

  local function spawn_enemy()
    local path = config.road
    local kind, definition = choose_enemy_type()
    local id = "ent_" .. kind .. "_" .. tostring(state.next_enemy_id)
    state.next_enemy_id = state.next_enemy_id + 1

    local wave_index = math.max(0, state.wave - 1)
    local health_multiplier = 1 + wave_index * config.enemy.health_growth_per_wave
    local speed_multiplier = 1 + wave_index * config.enemy.speed_growth_per_wave
    local health = math.floor(definition.health * health_multiplier + 0.5)
    local speed = definition.speed * speed_multiplier
    local reward_bonus = math.floor(wave_index / config.enemy.reward_growth_every)
    local reward = definition.reward + reward_bonus

    if not Entity.create(id, {
      name = definition.label,
      components = {
        IsoTransform = { tile = { path[1][1], path[1][2] }, height = 0.18, footprint = { 1, 1 } },
        Sprite = {
          texture = config.enemy.texture,
          size = config.enemy.size,
          pivot = { 0.5, 1.0 },
          color = definition.tint,
        },
      },
    }) then return end

    state.enemies[id] = {
      id = id,
      kind = kind,
      label = definition.label,
      health = health,
      max_health = health,
      reward = reward,
      base_damage = definition.base_damage,
      path = path,
      segment = 1,
      progress = 0,
      speed = speed,
      x = path[1][1],
      y = path[1][2],
    }
  end

  function self.start()
    if state.wave_active then
      state.status = "A wave is already active."
      return false
    end
    state.wave = state.wave + 1
    state.wave_active = true
    state.spawn_remaining = 5 + state.wave * 2
    state.spawn_timer = 0
    state.build_kind = nil
    Grid.clear_preview()
    state.status = "Wave " .. tostring(state.wave) .. " incoming!"
    return true
  end

  function self.clear()
    for id in pairs(state.enemies) do Entity.destroy(id) end
    state.enemies = {}
    state.spawn_remaining = 0
    state.wave_active = false
  end

  function self.update(dt)
    if not state.wave_active then return end
    state.spawn_timer = state.spawn_timer - dt
    if state.spawn_remaining > 0 and state.spawn_timer <= 0 then
      spawn_enemy()
      state.spawn_remaining = state.spawn_remaining - 1
      state.spawn_timer = math.max(
        config.enemy.minimum_spawn_interval,
        config.enemy.base_spawn_interval - (state.wave - 1) * config.enemy.spawn_interval_reduction)
    end

    local reached = {}
    for id, enemy in pairs(state.enemies) do
      enemy.progress = enemy.progress + enemy.speed * dt
      while enemy.progress >= 1 and enemy.segment < #enemy.path do
        enemy.progress = enemy.progress - 1
        enemy.segment = enemy.segment + 1
      end
      if enemy.segment >= #enemy.path then
        reached[#reached + 1] = id
      else
        local from = enemy.path[enemy.segment]
        local to = enemy.path[enemy.segment + 1]
        enemy.x = from[1] + (to[1] - from[1]) * enemy.progress
        enemy.y = from[2] + (to[2] - from[2]) * enemy.progress
        Grid.set_tile(id, enemy.x, enemy.y, 0.18)
      end
    end
    for _, id in ipairs(reached) do
      local enemy = state.enemies[id]
      Entity.destroy(id)
      state.enemies[id] = nil
      local damage = enemy and enemy.base_damage or 1
      state.base_health = math.max(0, state.base_health - damage)
      local label = enemy and enemy.label or "Enemy"
      state.status = label .. " reached the keep: -" .. tostring(damage) .. " health."
    end

    if state.spawn_remaining == 0 and next(state.enemies) == nil then
      state.wave_active = false
      state.gold = state.gold + 30 + state.wave * 5
      state.status = "Wave cleared. Build before the next attack."
    end
    if state.base_health <= 0 then
      self.clear()
      state.status = "The keep has fallen. Load a save or restart."
    end
  end

  return self
end

return Waves