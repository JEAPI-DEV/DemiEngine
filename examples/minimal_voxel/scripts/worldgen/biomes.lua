local config = require("worldgen.config")

local Biomes = {}

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
  return (top + ((bottom - top) * tz)) * 0.5 + 0.5
end

function Biomes.at(world_x, world_z)
  local settings = config.biomes
  if value_noise(settings, world_x, world_z) >= settings.desert_threshold then
    return settings.desert
  end
  return settings.plains
end

return Biomes
