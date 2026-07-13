local Waves = {}

function Waves.new(state, config)
  local self = {}

  local function spawn_enemy()
    local path = config.road
    local id = "ent_raider_" .. tostring(state.next_enemy_id)
    state.next_enemy_id = state.next_enemy_id + 1
    local health = config.enemy.base_health + state.wave * config.enemy.health_per_wave
    if not Entity.create(id, {
      name = "Raider",
      components = {
        IsoTransform = { tile = { path[1][1], path[1][2] }, height = 0.18, footprint = { 1, 1 } },
        Sprite = {
          texture = config.enemy.texture,
          size = config.enemy.size,
          pivot = { 0.5, 1.0 },
        },
      },
    }) then return end
    state.enemies[id] = {
      id = id,
      health = health,
      max_health = health,
      path = path,
      segment = 1,
      progress = 0,
      speed = config.enemy.base_speed + state.wave * 0.06,
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
      state.spawn_timer = 0.72
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
      Entity.destroy(id)
      state.enemies[id] = nil
      state.base_health = math.max(0, state.base_health - 1)
      state.status = "A raider reached the keep!"
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
