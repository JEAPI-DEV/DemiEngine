-- Route geometry stays independent of physics sampling and agent movement.
local Route = {}

local function intersects(ax, az, bx, bz, min_x, max_x, min_z, max_z)
  local first, last = 0, 1
  for _, axis in ipairs({ { ax, bx - ax, min_x, max_x }, { az, bz - az, min_z, max_z } }) do
    local origin, delta, low, high = table.unpack(axis)
    if math.abs(delta) < 1e-12 then
      if origin < low or origin > high then
        return false
      end
    else
      local enter, leave = (low - origin) / delta, (high - origin) / delta
      if enter > leave then
        enter, leave = leave, enter
      end
      first, last = math.max(first, enter), math.min(last, leave)
      if first > last then
        return false
      end
    end
  end
  return true
end

function Route.clear(surface, from, target)
  local ax, az, bx, bz = from[1], from[3], target[1], target[3]
  local grid, size = surface.grid, surface.cell_size
  local sx, sz = grid:world_to_cell(ax, az)
  local gx, gz = grid:world_to_cell(bx, bz)
  if not sx or not gx then
    return false
  end
  for _, box in ipairs(surface.obstacles) do
    local r = surface.radius
    local min_x, max_x = box.min_x - r, box.max_x + r
    local min_z, max_z = box.min_z - r, box.max_z + r
    -- If the actor starts inside clearance, keep the original first edge.
    -- A long shortcut must not use that exception to cross the whole building.
    if intersects(ax, az, bx, bz, min_x, max_x, min_z, max_z) then
      return false
    end
  end

  -- Traverse every touched cell, including both neighbours at grid corners.
  -- This uses cached survey data; shortcut checks do not cast physics rays.
  local cx, cz = sx, sz
  local dx, dz = bx - ax, bz - az
  local step_x, step_z = dx >= 0 and 1 or -1, dz >= 0 and 1 or -1
  local wx, wz = grid:cell_to_world(cx, cz)
  local next_x = dx == 0 and math.huge or (wx + step_x * size / 2 - ax) / dx
  local next_z = dz == 0 and math.huge or (wz + step_z * size / 2 - az) / dz
  local delta_x = dx == 0 and math.huge or size / math.abs(dx)
  local delta_z = dz == 0 and math.huge or size / math.abs(dz)
  local function blocked(x, z)
    return not (x == sx and z == sz) and grid:blocked(x, z)
  end
  local along_x_edge = dx == 0 and math.abs(ax - (wx - size / 2)) < 1e-9
  local along_z_edge = dz == 0 and math.abs(az - (wz - size / 2)) < 1e-9
  while true do
    if
      blocked(cx, cz)
      or (along_x_edge and blocked(cx - 1, cz))
      or (along_z_edge and blocked(cx, cz - 1))
    then
      return false
    end
    if cx == gx and cz == gz then
      return true
    end
    if math.min(next_x, next_z) >= 1 - 1e-12 then
      local cross_x, cross_z = next_x <= 1 + 1e-12, next_z <= 1 + 1e-12
      if cross_x and blocked(cx + step_x, cz) then
        return false
      end
      if cross_z and blocked(cx, cz + step_z) then
        return false
      end
      if cross_x and cross_z and blocked(cx + step_x, cz + step_z) then
        return false
      end
      return not blocked(gx, gz)
    end
    if math.abs(next_x - next_z) < 1e-9 then
      if blocked(cx + step_x, cz) or blocked(cx, cz + step_z) then
        return false
      end
      cx, cz = cx + step_x, cz + step_z
      next_x, next_z = next_x + delta_x, next_z + delta_z
    elseif next_x < next_z then
      cx, next_x = cx + step_x, next_x + delta_x
    else
      cz, next_z = cz + step_z, next_z + delta_z
    end
  end
end

function Route.simplify(surface, points)
  local result, index = { points[1] }, 1
  while index < #points do
    local next_index = index + 1
    for candidate = #points, index + 2, -1 do
      if Route.clear(surface, points[index], points[candidate]) then
        next_index = candidate
        break
      end
    end
    result[#result + 1], index = points[next_index], next_index
  end
  return result
end

return Route
