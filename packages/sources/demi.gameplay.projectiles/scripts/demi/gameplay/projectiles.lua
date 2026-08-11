local Projectiles = {}
Projectiles.__index = Projectiles

local function reset(projectile)
  projectile.active, projectile.owner, projectile.ignored, projectile.mask = false, nil, nil, nil
  projectile.x, projectile.y, projectile.vx, projectile.vy = 0, 0, 0, 0
  projectile.damage, projectile.life, projectile.pierce = 0, 0, 0
  projectile.hit_once = {}
end

function Projectiles.new(events, query)
  return setmetatable({ events = assert(events), query = assert(query), pool = {}, active = {} }, Projectiles)
end

function Projectiles:spawn(values)
  local projectile = table.remove(self.pool) or {}
  reset(projectile)
  projectile.active, projectile.owner = true, values.owner
  projectile.ignored, projectile.mask = values.ignored, values.mask
  projectile.x, projectile.y = values.x or 0, values.y or 0
  projectile.vx, projectile.vy = values.vx or 0, values.vy or 0
  projectile.damage, projectile.life = values.damage or 0, values.life or 1
  projectile.pierce = values.pierce or 0
  table.insert(self.active, projectile)
  self.events:emit("projectile_spawned", { owner = projectile.owner })
  return projectile
end

function Projectiles:release(projectile)
  if not projectile.active then return false end
  projectile.active = false
  for index = #self.active, 1, -1 do
    if self.active[index] == projectile then table.remove(self.active, index); break end
  end
  self.events:emit("projectile_released", { owner = projectile.owner })
  reset(projectile); table.insert(self.pool, projectile)
  return true
end

function Projectiles:update(dt)
  local snapshot = {}
  for index = 1, #self.active do snapshot[index] = self.active[index] end
  for _, projectile in ipairs(snapshot) do
    if projectile.active then
      local next_x, next_y = projectile.x + projectile.vx * dt, projectile.y + projectile.vy * dt
      local hits = self.query(projectile.x, projectile.y, next_x, next_y, projectile.mask) or {}
      table.sort(hits, function(a, b)
        return (a.fraction or 0) == (b.fraction or 0) and a.entity < b.entity or
          (a.fraction or 0) < (b.fraction or 0)
      end)
      for _, hit in ipairs(hits) do
        if hit.entity ~= projectile.owner and hit.entity ~= projectile.ignored and
          not projectile.hit_once[hit.entity] and not hit.trigger then
          projectile.hit_once[hit.entity] = true
          self.events:emit("damage_requested", { source = projectile.owner,
            target = hit.entity, amount = projectile.damage, point = hit.point, normal = hit.normal,
            tags = { "projectile" } })
          if projectile.pierce == 0 then self:release(projectile); break end
          projectile.pierce = projectile.pierce - 1
        end
      end
      if projectile.active then
        projectile.x, projectile.y = next_x, next_y
        projectile.life = projectile.life - dt
        if projectile.life <= 0 then self:release(projectile) end
      end
    end
  end
end

return Projectiles
