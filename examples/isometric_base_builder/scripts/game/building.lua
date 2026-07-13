local Building = {}

function Building.new(state, config)
  local self = {}

  function self.select(kind)
    if not config.towers[kind] then return end
    if state.wave_active then
      state.status = "Finish the wave before building."
      return
    end
    state.build_kind = kind
    state.selected_id = nil
    state.status = "Place " .. config.towers[kind].label .. " on a free tile."
  end

  function self.cancel()
    state.build_kind = nil
    Grid.clear_preview()
    state.status = "Build cancelled."
  end

  function self.preview(x, y)
    if not state.build_kind then
      Grid.clear_preview()
      return false
    end
    if config.road_cells[x .. "," .. y] then
      Grid.set_preview(x, y, 1, 1, false)
      return false, "ROAD_RESERVED"
    end
    local allowed, code = Grid.can_place(x, y, 1, 1)
    if allowed then
      local path = Grid.path_with_blocker(
        config.spawn[1], config.spawn[2], config.goal[1], config.goal[2],
        x, y, 1, 1)
      if not path then
        allowed = false
        code = "ROUTE_BLOCKED"
      end
    end
    Grid.set_preview(x, y, 1, 1, allowed)
    return allowed, code
  end

  function self.place(x, y)
    local kind = state.build_kind
    local definition = kind and config.towers[kind]
    if not definition then return false end
    local allowed, code = self.preview(x, y)
    if not allowed then
      state.status = "Cannot build: " .. tostring(code)
      return false
    end
    if state.gold < definition.cost then
      state.status = "Not enough gold for " .. definition.label .. "."
      return false
    end

    local id = "ent_player_tower_" .. tostring(state.next_tower_id)
    state.next_tower_id = state.next_tower_id + 1
    local created = Entity.create(id, {
      name = definition.label,
      components = {
        IsoTransform = { tile = { x, y }, height = 0.0, footprint = { 1, 1 } },
        Buildable = { asset = "", blocks_movement = true },
        Sprite = {
          texture = definition.texture,
          size = definition.size,
          pivot = { 0.5, 1.0 },
        },
      },
    })
    if not created then
      state.status = "Could not create tower entity."
      return false
    end

    state.gold = state.gold - definition.cost
    state.towers[id] = { id = id, kind = kind, x = x, y = y, cooldown = 0 }
    state.status = definition.label .. " built."
    state.build_kind = nil
    Grid.clear_preview()
    return true
  end

  function self.select_at(x, y)
    local id = nil
    for tower_id, tower in pairs(state.towers) do
      if tower.x == x and tower.y == y then
        id = tower_id
        break
      end
    end
    if id then
      state.selected_id = id
      state.status = config.towers[state.towers[id].kind].label .. " selected."
    else
      state.selected_id = nil
      state.status = "No tower on that tile."
    end
  end

  function self.restore(kind, x, y, saved_id)
    local definition = config.towers[kind]
    if not definition then return false end
    local id = saved_id
    if not id then
      id = "ent_player_tower_" .. tostring(state.next_tower_id)
      state.next_tower_id = state.next_tower_id + 1
    end
    if not Entity.create(id, {
      components = {
        IsoTransform = { tile = { x, y }, height = 0.0, footprint = { 1, 1 } },
        Buildable = { asset = "", blocks_movement = true },
        Sprite = {
          texture = definition.texture,
          size = definition.size,
          pivot = { 0.5, 1.0 },
        },
      },
    }) then return false end
    state.towers[id] = { id = id, kind = kind, x = x, y = y, cooldown = 0 }
    return true
  end

  return self
end

return Building
