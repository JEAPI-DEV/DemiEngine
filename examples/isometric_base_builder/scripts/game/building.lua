local TowerStats = require("game.tower_stats")
local Building = {}

function Building.new(state, config)
  local self = {}

  local function selected_tower()
    return state.selected_id and state.towers[state.selected_id]
  end

  function self.select(kind)
    if not config.towers[kind] then return end
    if state.wave_active then state.status = "Finish the wave before building." return end
    state.build_kind = kind
    state.selected_id = nil
    state.upgrade_menu_open = false
    state.status = "Place " .. config.towers[kind].label .. " on a free tile."
  end

  function self.cancel()
    state.build_kind = nil
    state.upgrade_menu_open = false
    Grid.clear_preview()
    state.status = "Build cancelled."
  end

  function self.preview(x, y)
    if not state.build_kind then Grid.clear_preview() return false end
    if config.road_cells[x .. "," .. y] then Grid.set_preview(x, y, 1, 1, false) return false, "ROAD_RESERVED" end
    local allowed, code = Grid.can_place(x, y, 1, 1)
    if allowed then
      local path = Grid.path_with_blocker(config.spawn[1], config.spawn[2], config.goal[1], config.goal[2], x, y, 1, 1)
      if not path then allowed = false code = "ROUTE_BLOCKED" end
    end
    Grid.set_preview(x, y, 1, 1, allowed)
    return allowed, code
  end

  function self.place(x, y)
    local kind = state.build_kind
    local definition = kind and config.towers[kind]
    if not definition then return false end
    local allowed, code = self.preview(x, y)
    if not allowed then state.status = "Cannot build: " .. tostring(code) return false end
    if state.gold < definition.cost then state.status = "Not enough gold for " .. definition.label .. "." return false end
    local id = "ent_player_tower_" .. tostring(state.next_tower_id)
    state.next_tower_id = state.next_tower_id + 1
    local created = Entity.create(id, { name = definition.label, components = {
      IsoTransform = { tile = { x, y }, height = 0.0, footprint = { 1, 1 } },
      Buildable = { asset = "", blocks_movement = true },
      Sprite = { texture = definition.texture, size = definition.size, pivot = { 0.5, 1.0 } },
    }})
    if not created then state.status = "Could not create tower entity." return false end
    state.gold = state.gold - definition.cost
    state.towers[id] = { id = id, kind = kind, x = x, y = y, cooldown = 0, power_level = 0, range_level = 0 }
    state.status = definition.label .. " built."
    state.build_kind = nil
    Grid.clear_preview()
    return true
  end

  function self.select_at(x, y)
    local id = nil
    for tower_id, tower in pairs(state.towers) do if tower.x == x and tower.y == y then id = tower_id break end end
    state.selected_id = id
    state.upgrade_menu_open = false
    if id then
      local tower = state.towers[id]
      local stats = TowerStats.get(tower, config.towers[tower.kind])
      state.status = config.towers[tower.kind].label .. " level " .. tostring(stats.level) .. " selected."
    else state.status = "No tower on that tile." end
  end

  function self.open_upgrade_menu()
    local tower = selected_tower()
    if not tower then return false end
    local definition = config.towers[tower.kind]
    local stats = TowerStats.get(tower, definition)
    if stats.level >= definition.upgrades.max_level then state.status = "This tower is already max level." return false end
    state.upgrade_menu_open = true
    state.status = "Choose RANGE or POWER."
    return true
  end

  function self.upgrade(choice)
    local tower = selected_tower()
    if not tower then return false end
    local definition = config.towers[tower.kind]
    local stats = TowerStats.get(tower, definition)
    if stats.level >= definition.upgrades.max_level then state.status = "This tower is already max level." return false end
    local cost = TowerStats.upgrade_cost(tower, definition)
    if state.gold < cost then state.status = "Not enough gold. Upgrade costs " .. tostring(cost) .. "." return false end
    state.gold = state.gold - cost
    if choice == "range" then tower.range_level = (tower.range_level or 0) + 1 else tower.power_level = (tower.power_level or 0) + 1 end
    state.upgrade_menu_open = false
    local updated = TowerStats.get(tower, definition)
    state.status = definition.label .. " upgraded to level " .. tostring(updated.level) .. "."
    return true
  end

  function self.destroy_selected()
    local tower = selected_tower()
    if not tower then return false end
    local definition = config.towers[tower.kind]
    local refund = math.floor(TowerStats.invested_gold(tower, definition) * definition.upgrades.destroy_refund + 0.5)
    Entity.destroy(tower.id)
    state.towers[tower.id] = nil
    state.selected_id = nil
    state.upgrade_menu_open = false
    state.gold = state.gold + refund
    state.status = definition.label .. " destroyed: +" .. tostring(refund) .. " gold."
    return true
  end

  function self.restore(kind, x, y, saved_id, power_level, range_level)
    local definition = config.towers[kind]
    if not definition then return false end
    local id = saved_id or ("ent_player_tower_" .. tostring(state.next_tower_id))
    if not saved_id then state.next_tower_id = state.next_tower_id + 1 end
    if not Entity.create(id, { components = {
      IsoTransform = { tile = { x, y }, height = 0.0, footprint = { 1, 1 } },
      Buildable = { asset = "", blocks_movement = true },
      Sprite = { texture = definition.texture, size = definition.size, pivot = { 0.5, 1.0 } },
    }}) then return false end
    state.towers[id] = { id = id, kind = kind, x = x, y = y, cooldown = 0, power_level = power_level or 0, range_level = range_level or 0 }
    return true
  end

  return self
end
return Building
