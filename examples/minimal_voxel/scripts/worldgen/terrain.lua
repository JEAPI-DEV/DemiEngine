local config = require("worldgen.config")

local Terrain = {}

function Terrain.chunk_id(cx, cz)
  return string.format("ent_chunk_%d_%d", cx, cz)
end

function Terrain.section_id(cx, section_y, cz)
  return string.format("ent_chunk_%d_%d_%d", cx, section_y, cz)
end

function Terrain.chunk_key(cx, cz)
  return cx .. ":" .. cz
end

function Terrain.section_key(cx, section_y, cz)
  return cx .. ":" .. section_y .. ":" .. cz
end

function Terrain.parse_section_key(key)
  local cx, section_y, cz = key:match("^(-?%d+):(-?%d+):(-?%d+)$")
  return tonumber(cx), tonumber(section_y), tonumber(cz)
end

function Terrain.column_key(world_x, world_z)
  return world_x .. ":" .. world_z
end

function Terrain.block_key(world_x, y, world_z)
  return world_x .. ":" .. y .. ":" .. world_z
end

function Terrain.parse_block_key(key)
  local world_x, y, world_z = key:match("^(-?%d+):(-?%d+):(-?%d+)$")
  return tonumber(world_x), tonumber(y), tonumber(world_z)
end

function Terrain.chunk_coord(value)
  return math.floor(value / config.chunk_size)
end

function Terrain.local_coord(value)
  return value - (Terrain.chunk_coord(value) * config.chunk_size)
end

function Terrain.section_coord(y)
  return math.floor(y / config.section_height)
end

function Terrain.local_section_coord(y)
  return y - (Terrain.section_coord(y) * config.section_height)
end

function Terrain.section_count()
  return math.ceil(config.chunk_height / config.section_height)
end

function Terrain.section_bounds(section_y)
  local min_y = section_y * config.section_height
  local max_y = math.min(min_y + config.section_height - 1, config.chunk_height - 1)
  return min_y, max_y
end

function Terrain.section_range_for_height(min_y, max_y)
  if max_y < min_y then
    return nil, nil
  end
  return Terrain.section_coord(min_y), Terrain.section_coord(max_y)
end

function Terrain.table_count(values)
  local count = 0
  for _ in pairs(values) do
    count = count + 1
  end
  return count
end

local function smoothstep(value)
  return value * value * (3.0 - 2.0 * value)
end

local function random_corner(seed, x, z)
  local value = math.sin((x * 127.1) + (z * 311.7) + (seed * 0.013)) * 43758.5453
  return value - math.floor(value)
end

local function value_noise(settings, world_x, world_z)
  local sample_x = world_x * settings.frequency
  local sample_z = world_z * settings.frequency
  local x0 = math.floor(sample_x)
  local z0 = math.floor(sample_z)
  local tx = smoothstep(sample_x - x0)
  local tz = smoothstep(sample_z - z0)
  local a = random_corner(settings.seed, x0, z0)
  local b = random_corner(settings.seed, x0 + 1, z0)
  local c = random_corner(settings.seed, x0, z0 + 1)
  local d = random_corner(settings.seed, x0 + 1, z0 + 1)
  local top = a + ((b - a) * tx)
  local bottom = c + ((d - c) * tx)
  return (top + ((bottom - top) * tz)) * 2.0 - 1.0
end

local function random_hash(seed, x, z)
  local value = math.sin((x * 91.7) + (z * 53.3) + (seed * 17.13)) * 24634.6345
  return value - math.floor(value)
end

local horizontal_neighbors = {
  { 1, 0 },
  { -1, 0 },
  { 0, 1 },
  { 0, -1 },
}

function Terrain.generated_height(world_x, world_z)
  local settings = config.terrain
  local noise =
    (value_noise(settings, world_x, world_z) * 0.5)
    + (value_noise(settings, world_x + 11.0, world_z) * 0.125)
    + (value_noise(settings, world_x - 11.0, world_z) * 0.125)
    + (value_noise(settings, world_x, world_z + 11.0) * 0.125)
    + (value_noise(settings, world_x, world_z - 11.0) * 0.125)
  local height = settings.base_height + math.floor((noise * settings.amplitude) + 0.5)
  height = math.floor(height / 2) * 2
  if height < 0 then
    return 0
  end
  if height >= config.chunk_height then
    return config.chunk_height - 1
  end
  return height
end

function Terrain.column_height(world, world_x, world_z)
  local columns = world and world.column_heights
  local key = Terrain.column_key(world_x, world_z)
  local edited = columns and columns[key]
  if edited ~= nil then
    return edited
  end
  local generated = world and world.generated_heights
  local cached = generated and generated[key]
  if cached ~= nil then
    return cached
  end
  local height = Terrain.generated_height(world_x, world_z)
  if generated ~= nil then
    generated[key] = height
  end
  return height
end

function Terrain.block_for_height(y, height)
  if y > height then
    return 0
  end
  if y == height then
    return config.terrain.cap_block
  end
  if y >= height - config.terrain.fill_depth then
    return config.terrain.fill_block
  end
  return config.terrain.base_block
end

local function base_block_at(world, world_x, y, world_z)
  if y < 0 or y >= config.chunk_height then
    return 0
  end
  local height = Terrain.column_height(world, world_x, world_z)
  if y > height then
    return 0
  end
  return Terrain.block_for_height(y, height)
end

function Terrain.terrain_block_at(world, world_x, y, world_z)
  if y < 0 or y >= config.chunk_height then
    return 0
  end
  local overrides = world and world.block_overrides
  local edited = overrides and overrides[Terrain.block_key(world_x, y, world_z)]
  if edited ~= nil then
    return edited
  end
  return base_block_at(world, world_x, y, world_z)
end

local function tree_origin_for_cell(cell_x, cell_z)
  local settings = config.trees
  if random_hash(settings.seed, cell_x, cell_z) > settings.chance then
    return nil
  end
  local offset_x = math.floor(random_hash(settings.seed + 13, cell_x, cell_z) * settings.cell_size)
  local offset_z = math.floor(random_hash(settings.seed + 29, cell_x, cell_z) * settings.cell_size)
  local world_x = (cell_x * settings.cell_size) + offset_x
  local world_z = (cell_z * settings.cell_size) + offset_z
  local ground_y = Terrain.generated_height(world_x, world_z)
  if ground_y < settings.min_height then
    return nil
  end
  return {
    x = world_x,
    y = ground_y + 1,
    z = world_z,
  }
end

local function tree_block_from_origin(origin, world_x, y, world_z)
  local settings = config.trees
  local dx = world_x - origin.x
  local dy = y - origin.y
  local dz = world_z - origin.z

  if dx == 0 and dz == 0 and dy >= 0 and dy < settings.trunk_height then
    return settings.log_block
  end

  local canopy_base = settings.trunk_height - 2
  local canopy_top = canopy_base + settings.canopy_height - 1
  if dy < canopy_base or dy > canopy_top then
    return 0
  end

  local horizontal_distance = math.abs(dx) + math.abs(dz)
  local radius = settings.canopy_radius
  if dy == canopy_top then
    radius = radius - 1
  end
  if math.abs(dx) <= radius and math.abs(dz) <= radius and horizontal_distance <= radius + 1 then
    return settings.leaves_block
  end
  return 0
end

function Terrain.feature_block_at(world, world_x, y, world_z)
  local settings = config.trees
  if not settings.enabled or y < 0 or y >= config.chunk_height then
    return 0
  end
  local cache = world and world.generated_feature_blocks
  if cache ~= nil then
    local chunk_key = Terrain.chunk_key(Terrain.chunk_coord(world_x), Terrain.chunk_coord(world_z))
    local chunk_blocks = cache[chunk_key]
    if chunk_blocks == nil then
      Terrain.feature_blocks_in_chunk(world, Terrain.chunk_coord(world_x), Terrain.chunk_coord(world_z))
      chunk_blocks = cache[chunk_key]
    end
    return (chunk_blocks and chunk_blocks[Terrain.block_key(world_x, y, world_z)]) or 0
  end

  local margin = settings.canopy_radius + 1
  local min_cell_x = math.floor((world_x - margin) / settings.cell_size)
  local max_cell_x = math.floor((world_x + margin) / settings.cell_size)
  local min_cell_z = math.floor((world_z - margin) / settings.cell_size)
  local max_cell_z = math.floor((world_z + margin) / settings.cell_size)
  local leaves_block = 0

  for cell_z = min_cell_z, max_cell_z do
    for cell_x = min_cell_x, max_cell_x do
      local origin = tree_origin_for_cell(cell_x, cell_z)
      if origin ~= nil then
        local block = tree_block_from_origin(origin, world_x, y, world_z)
        if block == settings.log_block then
          return block
        elseif block ~= 0 then
          leaves_block = block
        end
      end
    end
  end
  return leaves_block
end

local function collect_feature_blocks_in_chunk(cx, cz)
  local settings = config.trees
  local blocks = {}
  if not settings.enabled then
    return blocks
  end

  local min_x = cx * config.chunk_size
  local min_z = cz * config.chunk_size
  local max_x = min_x + config.chunk_size - 1
  local max_z = min_z + config.chunk_size - 1
  local margin = settings.canopy_radius + 1
  local min_cell_x = math.floor((min_x - margin) / settings.cell_size)
  local max_cell_x = math.floor((max_x + margin) / settings.cell_size)
  local min_cell_z = math.floor((min_z - margin) / settings.cell_size)
  local max_cell_z = math.floor((max_z + margin) / settings.cell_size)
  local emitted = {}

  for cell_z = min_cell_z, max_cell_z do
    for cell_x = min_cell_x, max_cell_x do
      local origin = tree_origin_for_cell(cell_x, cell_z)
      if origin ~= nil then
        for y = origin.y, origin.y + settings.trunk_height - 1 do
          if y >= 0 and y < config.chunk_height and origin.x >= min_x and origin.x <= max_x and origin.z >= min_z and origin.z <= max_z then
            local key = Terrain.block_key(origin.x, y, origin.z)
            if emitted[key] == nil then
              emitted[key] = true
              blocks[#blocks + 1] = { x = origin.x, y = y, z = origin.z, block = settings.log_block }
            end
          end
        end

        local canopy_base_y = origin.y + settings.trunk_height - 2
        for y = canopy_base_y, canopy_base_y + settings.canopy_height - 1 do
          for z = origin.z - settings.canopy_radius, origin.z + settings.canopy_radius do
            for x = origin.x - settings.canopy_radius, origin.x + settings.canopy_radius do
              if x >= min_x and x <= max_x and z >= min_z and z <= max_z then
                local block = tree_block_from_origin(origin, x, y, z)
                if block ~= 0 then
                  local key = Terrain.block_key(x, y, z)
                  if emitted[key] == nil then
                    emitted[key] = true
                    blocks[#blocks + 1] = { x = x, y = y, z = z, block = block }
                  end
                end
              end
            end
          end
        end
      end
    end
  end
  return blocks
end

function Terrain.feature_blocks_in_chunk(world, cx, cz)
  local cache = world and world.generated_features
  local key = Terrain.chunk_key(cx, cz)
  if cache ~= nil and cache[key] ~= nil then
    local block_cache = world and world.generated_feature_blocks
    if block_cache ~= nil and block_cache[key] == nil then
      local by_key = {}
      for _, entry in ipairs(cache[key]) do
        by_key[Terrain.block_key(entry.x, entry.y, entry.z)] = entry.block
      end
      block_cache[key] = by_key
    end
    return cache[key]
  end
  local blocks = collect_feature_blocks_in_chunk(cx, cz)
  if cache ~= nil then
    cache[key] = blocks
  end
  local block_cache = world and world.generated_feature_blocks
  if block_cache ~= nil then
    local by_key = {}
    for _, entry in ipairs(blocks) do
      by_key[Terrain.block_key(entry.x, entry.y, entry.z)] = entry.block
    end
    block_cache[key] = by_key
  end
  return blocks
end

function Terrain.for_feature_blocks_in_chunk(world, cx, cz, callback)
  for _, entry in ipairs(Terrain.feature_blocks_in_chunk(world, cx, cz)) do
    callback(entry.x, entry.y, entry.z, entry.block)
  end
end

function Terrain.active_section_range(world, cx, cz)
  local min_y = config.chunk_height
  local max_y = -1

  for z = -1, config.chunk_size do
    for x = -1, config.chunk_size do
      local world_x = (cx * config.chunk_size) + x
      local world_z = (cz * config.chunk_size) + z
      local height = Terrain.column_height(world, world_x, world_z)
      if x >= 0 and x < config.chunk_size and z >= 0 and z < config.chunk_size then
        min_y = math.min(min_y, height)
        max_y = math.max(max_y, height)

        for _, neighbor in ipairs(horizontal_neighbors) do
          local neighbor_height = Terrain.column_height(world, world_x + neighbor[1], world_z + neighbor[2])
          if height > neighbor_height then
            min_y = math.min(min_y, neighbor_height + 1)
            max_y = math.max(max_y, height)
          end
        end
      end
    end
  end

  Terrain.for_feature_blocks_in_chunk(world, cx, cz, function(_world_x, y, _world_z, _block)
    min_y = math.min(min_y, y)
    max_y = math.max(max_y, y)
  end)

  for key, block in pairs((world and world.block_overrides) or {}) do
    if block ~= 0 then
      local world_x, y, world_z = Terrain.parse_block_key(key)
      if world_x ~= nil and Terrain.chunk_coord(world_x) == cx and Terrain.chunk_coord(world_z) == cz then
        min_y = math.min(min_y, y)
        max_y = math.max(max_y, y)
      end
    end
  end

  return Terrain.section_range_for_height(math.max(0, min_y), math.min(config.chunk_height - 1, max_y))
end

function Terrain.block_at(world, world_x, y, world_z)
  if y < 0 or y >= config.chunk_height then
    return 0
  end
  local overrides = world and world.block_overrides
  local edited = overrides and overrides[Terrain.block_key(world_x, y, world_z)]
  if edited ~= nil then
    return edited
  end
  local feature_block = Terrain.feature_block_at(world, world_x, y, world_z)
  if feature_block ~= 0 then
    return feature_block
  end
  return base_block_at(world, world_x, y, world_z)
end

function Terrain.set_block(world, world_x, y, world_z, block)
  world.block_overrides = world.block_overrides or {}
  world.block_overrides[Terrain.block_key(world_x, y, world_z)] = block
end

function Terrain.surface_height(world, world_x, world_z)
  local height = Terrain.column_height(world, world_x, world_z)
  for y = height, 0, -1 do
    local block = Terrain.terrain_block_at(world, world_x, y, world_z)
    if block ~= 0 then
      return y, block
    end
  end
  return -1, 0
end

return Terrain
