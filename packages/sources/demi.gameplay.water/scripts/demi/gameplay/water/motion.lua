-- Private shared math. Body volume and swimming policy belong to this package,
-- not to the native prepared-water query service.
local Motion = {}

function Motion.number(value, name, minimum, positive)
  assert(type(value) == "number" and value == value and math.abs(value) < math.huge,
    name .. " must be finite")
  assert(value >= minimum and (not positive or value > minimum),
    name .. " is outside its allowed range")
  return value
end

function Motion.vector(value, name)
  assert(type(value) == "table", name .. " must be a three-number array")
  local count = 0
  for key in pairs(value) do
    assert(type(key) == "number" and key % 1 == 0 and key >= 1 and key <= 3,
      name .. " must be a dense three-number array")
    count = count + 1
  end
  assert(count == 3, name .. " must contain three coordinates")
  return {
    Motion.number(value[1], name, -math.huge),
    Motion.number(value[2], name, -math.huge),
    Motion.number(value[3], name, -math.huge),
  }
end

function Motion.value(options, key, fallback)
  if options[key] == nil then
    return fallback
  end
  return options[key]
end

function Motion.options(entity_id, options)
  assert(type(entity_id) == "string" and entity_id ~= "", "entity ID is required")
  assert(options == nil or type(options) == "table", "options must be a table")
  options = options or {}
  if options.terrain_id ~= nil then
    assert(type(options.terrain_id) == "string" and options.terrain_id ~= "",
      "terrain_id must be a nonempty string")
  end
  return options
end

function Motion.submersion(sample, position, half_height)
  if not sample then
    return 0
  end
  -- Native depth and surface projection share column_up, including rotated and
  -- scaled terrain. The river surface normal is not this measurement axis.
  local surface = sample.surface
  local up = sample.column_up
  local surface_offset = 0
  for axis = 1, 3 do
    surface_offset = surface_offset + (surface[axis] - position[axis]) * up[axis]
  end
  local water_bottom = surface_offset - sample.depth
  local overlap = math.min(half_height, surface_offset)
    - math.max(-half_height, water_bottom)
  return math.max(0, math.min(1, overlap / (2 * half_height)))
end

function Motion.drag_delta(relative_velocity, rate, immersion, dt)
  -- Exponential damping composes across fixed-step subdivisions and cannot
  -- reverse relative velocity even when a game takes a large fixed step.
  return relative_velocity * (math.exp(-rate * immersion * dt) - 1)
end

function Motion.clamp_direction(value)
  local scale = math.max(math.abs(value[1]), math.abs(value[2]), math.abs(value[3]))
  if scale == 0 then
    return value
  end
  local x, y, z = value[1] / scale, value[2] / scale, value[3] / scale
  local length = math.sqrt(x*x + y*y + z*z)
  if scale <= 1 and scale * length <= 1 then
    return value
  end
  return {x / length, y / length, z / length}
end

return Motion
