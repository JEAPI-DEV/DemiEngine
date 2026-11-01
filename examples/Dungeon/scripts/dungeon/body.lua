-- Shared sprite and combat geometry. Ground colliders remain independent.
local Body = {}

function Body.scale(actor)
  return actor.kind == "boss" and 2.6 or (actor.kind == "sentinel" and 1.25 or 1)
end

function Body.sprite(actor)
  local scale = Body.scale(actor)
  -- The renderer measures pivot Y from the top of the image.
  return { scale, 1.5 * scale }, { 0.5, 0.85 }
end

function Body.bounds(actor)
  local size, pivot = Body.sprite(actor)
  return actor.x - size[1] * pivot[1],
    actor.y - size[2] * (1 - pivot[2]),
    actor.x + size[1] * (1 - pivot[1]),
    actor.y + size[2] * pivot[2]
end

function Body.muzzle(actor)
  return actor.x, actor.y + 0.55 * Body.scale(actor)
end

-- Slab intersection returns the earliest hit, not a center-distance proxy.
function Body.intersection(bolt, actor, distance)
  local left, bottom, right, top = Body.bounds(actor)
  local radius = 0.125
  local entry, exit = 0, distance
  local function axis(origin, direction, minimum, maximum)
    minimum, maximum = minimum - radius, maximum + radius
    if math.abs(direction) < 0.000001 then
      return origin >= minimum and origin <= maximum
    end
    local first, last = (minimum - origin) / direction, (maximum - origin) / direction
    if first > last then
      first, last = last, first
    end
    entry, exit = math.max(entry, first), math.min(exit, last)
    return entry <= exit
  end
  if axis(bolt.x, bolt.dx, left, right) and axis(bolt.y, bolt.dy, bottom, top) then
    return entry
  end
end

return Body
