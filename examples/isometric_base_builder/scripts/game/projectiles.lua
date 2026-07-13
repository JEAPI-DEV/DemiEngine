local Projectiles = {}

local function distance(x1, y1, x2, y2)
  local dx, dy = x2 - x1, y2 - y1
  return math.sqrt(dx * dx + dy * dy), dx, dy
end

function Projectiles.new(state)
  local self = {}

  function self.spawn(tower, target, definition)
    local id = "ent_projectile_" .. tostring(state.next_projectile_id)
    state.next_projectile_id = state.next_projectile_id + 1
    local presentation = definition.projectile
    if not Entity.create(id, {
      name = definition.label .. " projectile",
      components = {
        IsoTransform = {
          tile = { tower.x, tower.y },
          height = 0.85,
          footprint = { 1, 1 },
        },
        Sprite = {
          texture = presentation.texture,
          size = presentation.size,
          pivot = { 0.5, 0.5 },
        },
      },
    }) then return false end

    state.projectiles[id] = {
      id = id,
      target_id = target.id,
      x = tower.x,
      y = tower.y,
      damage = definition.damage,
      speed = presentation.speed,
    }
    return true
  end

  function self.update(dt)
    local defeated = {}
    local removed = {}
    for id, projectile in pairs(state.projectiles) do
      local target = state.enemies[projectile.target_id]
      if not target or target.health <= 0 then
        removed[#removed + 1] = id
      else
        local remaining, dx, dy = distance(
          projectile.x, projectile.y, target.x, target.y)
        local travel = projectile.speed * dt
        if remaining <= math.max(travel, 0.08) then
          target.health = target.health - projectile.damage
          if target.health <= 0 then defeated[target.id] = true end
          removed[#removed + 1] = id
        else
          projectile.x = projectile.x + dx / remaining * travel
          projectile.y = projectile.y + dy / remaining * travel
          Grid.set_tile(id, projectile.x, projectile.y, 0.85)
        end
      end
    end
    for _, id in ipairs(removed) do
      Entity.destroy(id)
      state.projectiles[id] = nil
    end
    return defeated
  end

  function self.clear()
    for id in pairs(state.projectiles) do Entity.destroy(id) end
    state.projectiles = {}
  end

  return self
end

return Projectiles
