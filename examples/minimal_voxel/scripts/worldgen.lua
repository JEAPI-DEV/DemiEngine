local Worldgen = {}

local chunk_size = 16
local chunk_height = 24
local load_radius = 3
local unload_radius = load_radius + 1
local camera_id = "ent_camera"
local benchmark_enabled = os.getenv("DEMI_VOXEL_BENCH") == "1"
local benchmark_step = 1.0

local terrain = {
  enabled = true,
  seed = 4242,
  base_height = 8,
  amplitude = 7,
  frequency = 0.045,
  dirt_depth = 4,
  surface_block = "grass",
  subsurface_block = "dirt",
  stone_block = "stone",
}

local function chunk_id(cx, cz)
  return string.format("ent_chunk_%d_%d", cx, cz)
end

local function chunk_key(cx, cz)
  return cx .. ":" .. cz
end

local function chunk_coord(value)
  return math.floor(value / chunk_size)
end

local function create_chunk(self, cx, cz)
  Entity.create(chunk_id(cx, cz), {
    name = string.format("Terrain Chunk %d %d", cx, cz),
    components = {
      Transform3D = {
        position = { cx * chunk_size, 0.0, cz * chunk_size },
        rotation = { 0.0, 0.0, 0.0 },
        scale = { 1.0, 1.0, 1.0 },
      },
      VoxelChunk = {
        block_set = "asset://blocksets/basic",
        dimensions = { chunk_size, chunk_height, chunk_size },
        debug = false,
        terrain = terrain,
      },
    },
  })
  self.loaded[chunk_key(cx, cz)] = { x = cx, z = cz }
end

local function destroy_chunk(self, key, chunk)
  Entity.destroy(chunk_id(chunk.x, chunk.z))
  self.loaded[key] = nil
end

local function sync_chunks(self, center_x, center_z)
  for cz = center_z - load_radius, center_z + load_radius do
    for cx = center_x - load_radius, center_x + load_radius do
      local key = chunk_key(cx, cz)
      if self.loaded[key] == nil then
        create_chunk(self, cx, cz)
      end
    end
  end

  local stale = {}
  for key, chunk in pairs(self.loaded) do
    if math.abs(chunk.x - center_x) > unload_radius or math.abs(chunk.z - center_z) > unload_radius then
      stale[#stale + 1] = { key = key, chunk = chunk }
    end
  end

  for _, entry in ipairs(stale) do
    destroy_chunk(self, entry.key, entry.chunk)
  end
end

function Worldgen:on_start()
  self.loaded = {}
  self.center_x = nil
  self.center_z = nil
  self.benchmark_x = 0.0
  self:on_update(0.0)
end

function Worldgen:on_update(_dt)
  if benchmark_enabled then
    self.benchmark_x = self.benchmark_x + benchmark_step
    Transform3D.set_position(camera_id, self.benchmark_x, 18.0, 44.0)
  end

  local x, _y, z = Transform3D.get_position(camera_id)
  if x == nil or z == nil then
    return
  end

  local center_x = chunk_coord(x)
  local center_z = chunk_coord(z)
  if center_x ~= self.center_x or center_z ~= self.center_z then
    self.center_x = center_x
    self.center_z = center_z
    sync_chunks(self, center_x, center_z)
  end
end

return Worldgen
