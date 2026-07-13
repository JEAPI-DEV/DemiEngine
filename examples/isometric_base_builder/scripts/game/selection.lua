local Selection = {}

local SEGMENTS = 64

function Selection.update(state, config)
  Debug.clear_lines()
  local tower = state.selected_id and state.towers[state.selected_id]
  if not tower then return end
  local definition = config.towers[tower.kind]
  if not definition then return end

  local previous_x, previous_y = nil, nil
  for index = 0, SEGMENTS do
    local angle = index / SEGMENTS * math.pi * 2.0
    local tile_x = tower.x + math.cos(angle) * definition.range
    local tile_y = tower.y + math.sin(angle) * definition.range
    local world_x, world_y = Grid.tile_to_world(tile_x, tile_y, 0.03)
    if previous_x then
      Debug.line(previous_x, previous_y, world_x, world_y,
        0.25, 0.92, 0.78, 0.75)
    end
    previous_x, previous_y = world_x, world_y
  end
end

return Selection
