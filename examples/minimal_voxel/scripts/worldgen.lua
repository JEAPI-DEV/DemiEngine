local Worldgen = {}

local chunk_size = 16
local chunk_height = 24
local load_radius = 3
local unload_radius = load_radius + 1
local camera_id = "ent_camera"
local benchmark_enabled = os.getenv("DEMI_VOXEL_BENCH") == "1"
local benchmark_step = 1.0
local chunk_load_budget = tonumber(os.getenv("DEMI_VOXEL_CHUNK_LOADS_PER_FRAME") or "") or 1
local chunk_unload_budget = tonumber(os.getenv("DEMI_VOXEL_CHUNK_UNLOADS_PER_FRAME") or "") or 16

local terrain = {
  seed = 4242,
  base_height = 8,
  amplitude = 7,
  frequency = 0.045,
  fill_depth = 4,
  cap_block = 1,
  fill_block = 2,
  base_block = 3,
}

local atlas_columns = 4
local block_tiles = {
  [1] = { top = 0, side = 1, bottom = 2 },
  [2] = { top = 2, side = 2, bottom = 2 },
  [3] = { top = 3, side = 3, bottom = 3 },
}

local faces = {
  {
    normal = { 1.0, 0.0, 0.0 },
    neighbor = { 1, 0, 0 },
    tile = "side",
    corners = {
      { 1.0, 0.0, 1.0 },
      { 1.0, 0.0, 0.0 },
      { 1.0, 1.0, 0.0 },
      { 1.0, 1.0, 1.0 },
    },
  },
  {
    normal = { -1.0, 0.0, 0.0 },
    neighbor = { -1, 0, 0 },
    tile = "side",
    corners = {
      { 0.0, 0.0, 0.0 },
      { 0.0, 0.0, 1.0 },
      { 0.0, 1.0, 1.0 },
      { 0.0, 1.0, 0.0 },
    },
  },
  {
    normal = { 0.0, 1.0, 0.0 },
    neighbor = { 0, 1, 0 },
    tile = "top",
    corners = {
      { 0.0, 1.0, 1.0 },
      { 1.0, 1.0, 1.0 },
      { 1.0, 1.0, 0.0 },
      { 0.0, 1.0, 0.0 },
    },
  },
  {
    normal = { 0.0, -1.0, 0.0 },
    neighbor = { 0, -1, 0 },
    tile = "bottom",
    corners = {
      { 0.0, 0.0, 0.0 },
      { 1.0, 0.0, 0.0 },
      { 1.0, 0.0, 1.0 },
      { 0.0, 0.0, 1.0 },
    },
  },
  {
    normal = { 0.0, 0.0, 1.0 },
    neighbor = { 0, 0, 1 },
    tile = "side",
    corners = {
      { 0.0, 0.0, 1.0 },
      { 1.0, 0.0, 1.0 },
      { 1.0, 1.0, 1.0 },
      { 0.0, 1.0, 1.0 },
    },
  },
  {
    normal = { 0.0, 0.0, -1.0 },
    neighbor = { 0, 0, -1 },
    tile = "side",
    corners = {
      { 1.0, 0.0, 0.0 },
      { 0.0, 0.0, 0.0 },
      { 0.0, 1.0, 0.0 },
      { 1.0, 1.0, 0.0 },
    },
  },
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

local function table_count(values)
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

local function terrain_height(settings, world_x, world_z)
  local height = settings.base_height + math.floor((value_noise(settings, world_x, world_z) * settings.amplitude) + 0.5)
  if height < 0 then
    return 0
  end
  if height >= chunk_height then
    return chunk_height - 1
  end
  return height
end

local function block_for_height(y, height)
  if y > height then
    return 0
  end
  if y == height then
    return terrain.cap_block
  end
  if y >= height - terrain.fill_depth then
    return terrain.fill_block
  end
  return terrain.base_block
end

local function tile_uvs(tile)
  local u0 = tile / atlas_columns
  local u1 = (tile + 1) / atlas_columns
  return {
    { u0, 1.0 },
    { u1, 1.0 },
    { u1, 0.0 },
    { u0, 0.0 },
  }
end

local function push_vertex(mesh, x, y, z, normal, uv)
  mesh.vertices[#mesh.vertices + 1] = { x, y, z }
  mesh.normals[#mesh.normals + 1] = normal
  mesh.uvs[#mesh.uvs + 1] = uv
end

local function append_face(mesh, x, y, z, block, face)
  local tiles = block_tiles[block]
  local tile = tiles[face.tile] or tiles.side
  local uvs = tile_uvs(tile)
  local order = { 1, 2, 3, 1, 3, 4 }
  for _, corner_index in ipairs(order) do
    local corner = face.corners[corner_index]
    push_vertex(mesh, x + corner[1], y + corner[2], z + corner[3], face.normal, uvs[corner_index])
  end
end

local function build_chunk_mesh(cx, cz)
  local mesh = { vertices = {}, normals = {}, uvs = {} }
  local top_face = faces[3]
  local side_faces = { faces[1], faces[2], faces[5], faces[6] }
  for z = 0, chunk_size - 1 do
    for x = 0, chunk_size - 1 do
      local world_x = (cx * chunk_size) + x
      local world_z = (cz * chunk_size) + z
      local height = terrain_height(terrain, world_x, world_z)
      append_face(mesh, x, height, z, terrain.cap_block, top_face)

      for _, face in ipairs(side_faces) do
        local neighbor = face.neighbor
        local neighbor_height = terrain_height(terrain, world_x + neighbor[1], world_z + neighbor[3])
        for y = neighbor_height + 1, height do
          append_face(mesh, x, y, z, block_for_height(y, height), face)
        end
      end
    end
  end
  return mesh
end

local function queue_chunk_load(self, cx, cz, center_x, center_z)
  local key = chunk_key(cx, cz)
  if self.loaded[key] ~= nil or self.queued_loads[key] then
    return
  end
  self.queued_loads[key] = true
  self.pending_loads[#self.pending_loads + 1] = {
    key = key,
    x = cx,
    z = cz,
    distance = ((cx - center_x) * (cx - center_x)) + ((cz - center_z) * (cz - center_z)),
  }
end

local function queue_chunk_unload(self, key, chunk)
  if self.queued_unloads[key] then
    return
  end
  self.queued_unloads[key] = true
  self.pending_unloads[#self.pending_unloads + 1] = { key = key, chunk = chunk }
end

local function create_chunk(self, cx, cz)
  local key = chunk_key(cx, cz)
  local mesh = build_chunk_mesh(cx, cz)
  Entity.create(chunk_id(cx, cz), {
    name = string.format("Terrain Chunk %d %d", cx, cz),
    components = {
      Transform3D = {
        position = { cx * chunk_size, 0.0, cz * chunk_size },
        rotation = { 0.0, 0.0, 0.0 },
        scale = { 1.0, 1.0, 1.0 },
      },
      MeshRenderer = {
        texture = "asset://blocksets/basic",
        vertices = mesh.vertices,
        normals = mesh.normals,
        uvs = mesh.uvs,
      },
    },
  })
  self.loaded[key] = { x = cx, z = cz }
end

local function destroy_chunk(self, key, chunk)
  Entity.destroy(chunk_id(chunk.x, chunk.z))
  self.loaded[key] = nil
end

local function process_chunk_queues(self)
  for _ = 1, chunk_load_budget do
    local entry = table.remove(self.pending_loads, 1)
    if entry == nil then
      break
    end
    self.queued_loads[entry.key] = nil
    if self.desired[entry.key] and self.loaded[entry.key] == nil then
      create_chunk(self, entry.x, entry.z)
    end
  end

  for _ = 1, chunk_unload_budget do
    local entry = table.remove(self.pending_unloads, 1)
    if entry == nil then
      break
    end
    self.queued_unloads[entry.key] = nil
    local chunk = entry.chunk
    local outside_unload_radius =
      math.abs(chunk.x - self.center_x) > unload_radius or math.abs(chunk.z - self.center_z) > unload_radius
    if self.loaded[entry.key] ~= nil and outside_unload_radius then
      destroy_chunk(self, entry.key, entry.chunk)
    end
  end
end

local function sync_chunks(self, center_x, center_z)
  self.desired = {}
  for cz = center_z - load_radius, center_z + load_radius do
    for cx = center_x - load_radius, center_x + load_radius do
      local key = chunk_key(cx, cz)
      self.desired[key] = true
      if self.loaded[key] == nil then
        queue_chunk_load(self, cx, cz, center_x, center_z)
      end
    end
  end

  for key, chunk in pairs(self.loaded) do
    if math.abs(chunk.x - center_x) > unload_radius or math.abs(chunk.z - center_z) > unload_radius then
      queue_chunk_unload(self, key, chunk)
    end
  end

  table.sort(self.pending_loads, function(a, b)
    if a.distance == b.distance then
      return a.key < b.key
    end
    return a.distance < b.distance
  end)
end

function Worldgen:on_start()
  self.loaded = {}
  self.desired = {}
  self.pending_loads = {}
  self.pending_unloads = {}
  self.queued_loads = {}
  self.queued_unloads = {}
  self.center_x = nil
  self.center_z = nil
  self.benchmark_x = 0.0
  Hud.text("hud_chunks", "chunks: loading", 20.0, 116.0, 3.0)
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

  process_chunk_queues(self)
  Hud.set_text(
    "hud_chunks",
    string.format("chunks: %d loaded, %d queued", table_count(self.loaded), #self.pending_loads + #self.pending_unloads)
  )
end

return Worldgen
