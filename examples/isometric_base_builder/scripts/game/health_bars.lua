local HealthBars = {}

local BAR_HEIGHT = 1.25
local BAR_WIDTH = 0.86
local FILL_WIDTH = 0.78

local function destroy_bar(bar)
  Entity.destroy_many({ bar.background_id, bar.fill_id })
end

local function create_bar(enemy)
  local background_id = "ent_health_bg_" .. enemy.id
  local fill_id = "ent_health_fill_" .. enemy.id
  local child_transform = {
    parent = enemy.id,
    tile = { 0.0, 0.0 },
    height = BAR_HEIGHT,
  }

  local background_created = Entity.create(background_id, {
    components = {
      IsoTransform = child_transform,
      Sprite = {
        shape = "rectangle",
        size = { BAR_WIDTH, 0.13 },
        color = { 0.10, 0.11, 0.14, 0.9 },
      },
    },
  })
  local fill_created = Entity.create(fill_id, {
    components = {
      IsoTransform = child_transform,
      Sprite = {
        shape = "rectangle",
        size = { FILL_WIDTH, 0.075 },
        color = { 0.22, 0.88, 0.40, 1.0 },
      },
    },
  })
  if not background_created or not fill_created then
    Entity.destroy_many({ background_id, fill_id })
    return nil
  end
  return {
    owner_id = enemy.id,
    background_id = background_id,
    fill_id = fill_id,
  }
end

local function update_fill(bar, enemy)
  local ratio = math.max(0.0, math.min(1.0,
    enemy.health / math.max(enemy.max_health, 1)))
  local width = FILL_WIDTH * ratio
  Sprite2D.set_size(bar.fill_id, width, 0.075)

  -- Keep the fill's left edge fixed while its exact width changes. Equal and
  -- opposite tile offsets move horizontally in the isometric projection.
  local world_offset = -FILL_WIDTH * 0.5 + width * 0.5
  local tile_offset = world_offset * 0.5
  Grid.set_tile(bar.fill_id, tile_offset, -tile_offset, BAR_HEIGHT)
end

function HealthBars.update(state)
  for id, enemy in pairs(state.enemies) do
    local bar = state.health_bars[id]
    if not bar then
      bar = create_bar(enemy)
      state.health_bars[id] = bar
    end
    if bar then update_fill(bar, enemy) end
  end

  for id, bar in pairs(state.health_bars) do
    if not state.enemies[id] then
      destroy_bar(bar)
      state.health_bars[id] = nil
    end
  end
end

function HealthBars.clear(state)
  for _, bar in pairs(state.health_bars) do destroy_bar(bar) end
  state.health_bars = {}
end

return HealthBars
