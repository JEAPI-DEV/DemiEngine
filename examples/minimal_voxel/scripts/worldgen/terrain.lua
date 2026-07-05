local config = require("worldgen.config")

local Terrain = {}

function Terrain.chunk_id(cx, cz)
  return string.format("ent_chunk_%d_%d", cx, cz)
end

function Terrain.chunk_key(cx, cz)
  return cx .. ":" .. cz
end

function Terrain.column_key(world_x, world_z)
  return world_x .. ":" .. world_z
end

function Terrain.chunk_coord(value)
  return math.floor(value / config.chunk_size)
end

function Terrain.local_coord(value)
  return value - (Terrain.chunk_coord(value) * config.chunk_size)
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
  local edited = world.column_heights[Terrain.column_key(world_x, world_z)]
  if edited ~= nil then
    return edited
  end
  return Terrain.generated_height(world_x, world_z)
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

function Terrain.block_at(world, world_x, y, world_z)
  if y < 0 or y >= config.chunk_height then
    return 0
  end
  local height = Terrain.column_height(world, world_x, world_z)
  if y > height then
    return 0
  end
  return Terrain.block_for_height(y, height)
end

return Terrain
