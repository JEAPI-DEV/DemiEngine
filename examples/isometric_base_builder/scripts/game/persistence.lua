local Persistence = {}

function Persistence.new(state, config, building, waves, projectiles, health_bars)
  local self = {}

  function self.save()
    local towers = {}
    for _, tower in pairs(state.towers) do
      towers[#towers + 1] = {
        id = tower.id,
        kind = tower.kind,
        x = tower.x,
        y = tower.y,
        power_level = tower.power_level or 0,
        range_level = tower.range_level or 0,
      }
    end
    table.sort(towers, function(a, b)
      if a.y == b.y then return a.x < b.x end
      return a.y < b.y
    end)
    local selected_entities = {}
    if state.selected_id then selected_entities.primary = state.selected_id end
    local ok = Save.write_state(config.save_slot, {
      game = {
        gold = state.gold,
        base_health = state.base_health,
        wave = state.wave,
      },
      selected_entities = selected_entities,
      prefab_instances = { towers = towers },
      lua = { next_tower_id = state.next_tower_id },
    }, { autosave = false, reason = "player_save" })
    state.status = ok and "Village saved." or ("Save failed: " .. Save.last_error())
    return ok
  end

  function self.load()
    local document = Save.read_state(config.save_slot)
    local saved = document and document.game
    if not saved then
      state.status = "No village save exists yet."
      return false
    end
    waves.clear()
    projectiles.clear()
    health_bars.clear(state)
    for id in pairs(state.towers) do Entity.destroy(id) end
    state.towers = {}
    state.next_tower_id = 1
    state.gold = saved.gold or config.start_gold
    state.base_health = saved.base_health or config.start_health
    state.wave = saved.wave or 0
    local prefab_instances = document.prefab_instances or {}
    for _, tower in ipairs(prefab_instances.towers or saved.towers or {}) do
      building.restore(tower.kind, tower.x, tower.y, tower.id, tower.power_level, tower.range_level)
    end
    state.build_kind = nil
    state.upgrade_menu_open = false
    local saved_selection = document.selected_entities or {}
    state.selected_id = state.towers[saved_selection.primary] and saved_selection.primary or nil
    local saved_lua = document.lua or {}
    state.next_tower_id = math.max(state.next_tower_id, saved_lua.next_tower_id or 1)
    Grid.clear_preview()
    state.status = "Village loaded."
    return true
  end

  return self
end

return Persistence
