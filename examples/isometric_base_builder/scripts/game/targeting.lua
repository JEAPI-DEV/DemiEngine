local Targeting = {}

Targeting.modes = {
  random = true,
  strongest = true,
  weakest = true,
  first = true,
}

local function route_progress(enemy)
  return (enemy.segment or 0) + (enemy.progress or 0)
end

local function is_better(candidate, current, mode)
  if not current then return true end

  if mode == "strongest" or mode == "weakest" then
    if candidate.health ~= current.health then
      return mode == "strongest" and candidate.health > current.health or candidate.health < current.health
    end
  else -- "first" follows the enemy furthest along the route.
    local candidate_progress = route_progress(candidate)
    local current_progress = route_progress(current)
    if candidate_progress ~= current_progress then return candidate_progress > current_progress end
  end
  return candidate.id < current.id
end

function Targeting.choose(tower, enemies, range_squared)
  local candidates = {}
  for _, enemy in pairs(enemies) do
    local dx, dy = tower.x - enemy.x, tower.y - enemy.y
    if dx * dx + dy * dy <= range_squared then
      candidates[#candidates + 1] = enemy
    end
  end
  if #candidates == 0 then return nil end

  local mode = Targeting.modes[tower.targeting] and tower.targeting or "first"
  if mode == "random" then
    table.sort(candidates, function(a, b) return a.id < b.id end)
    return candidates[math.random(#candidates)]
  end

  local target = nil
  for _, candidate in ipairs(candidates) do
    if is_better(candidate, target, mode) then target = candidate end
  end
  return target
end

return Targeting
