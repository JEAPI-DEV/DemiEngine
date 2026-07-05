local config = require("worldgen.config")
local mesh_builder = require("worldgen.mesh")
local terrain = require("worldgen.terrain")

local Chunks = {}

function Chunks.create(world, cx, cz)
  local mesh = mesh_builder.build_chunk(world, cx, cz)
  Entity.create(terrain.chunk_id(cx, cz), {
    name = string.format("Terrain Chunk %d %d", cx, cz),
    components = {
      Transform3D = {
        position = { cx * config.chunk_size, 0.0, cz * config.chunk_size },
        rotation = { 0.0, 0.0, 0.0 },
        scale = { 1.0, 1.0, 1.0 },
      },
      MeshRenderer = {
        texture = config.pack.texture,
        vertices = mesh.vertices,
        normals = mesh.normals,
        uvs = mesh.uvs,
      },
    },
  })
  world.loaded[terrain.chunk_key(cx, cz)] = { x = cx, z = cz }
end

function Chunks.destroy(world, key, chunk)
  Entity.destroy(terrain.chunk_id(chunk.x, chunk.z))
  world.loaded[key] = nil
end

function Chunks.rebuild(world, cx, cz)
  if world.loaded[terrain.chunk_key(cx, cz)] == nil then
    return
  end
  Entity.destroy(terrain.chunk_id(cx, cz))
  Chunks.create(world, cx, cz)
end

function Chunks.rebuild_column(world, world_x, world_z)
  local cx = terrain.chunk_coord(world_x)
  local cz = terrain.chunk_coord(world_z)
  Chunks.rebuild(world, cx, cz)
  if terrain.local_coord(world_x) == 0 then
    Chunks.rebuild(world, cx - 1, cz)
  elseif terrain.local_coord(world_x) == config.chunk_size - 1 then
    Chunks.rebuild(world, cx + 1, cz)
  end
  if terrain.local_coord(world_z) == 0 then
    Chunks.rebuild(world, cx, cz - 1)
  elseif terrain.local_coord(world_z) == config.chunk_size - 1 then
    Chunks.rebuild(world, cx, cz + 1)
  end
end

local function queue_load(world, cx, cz, center_x, center_z)
  local key = terrain.chunk_key(cx, cz)
  if world.loaded[key] ~= nil or world.queued_loads[key] then
    return
  end
  world.queued_loads[key] = true
  world.pending_loads[#world.pending_loads + 1] = {
    key = key,
    x = cx,
    z = cz,
    distance = ((cx - center_x) * (cx - center_x)) + ((cz - center_z) * (cz - center_z)),
  }
end

local function queue_unload(world, key, chunk)
  if world.queued_unloads[key] then
    return
  end
  world.queued_unloads[key] = true
  world.pending_unloads[#world.pending_unloads + 1] = { key = key, chunk = chunk }
end

function Chunks.sync(world, center_x, center_z)
  world.desired = {}
  for cz = center_z - config.load_radius, center_z + config.load_radius do
    for cx = center_x - config.load_radius, center_x + config.load_radius do
      local key = terrain.chunk_key(cx, cz)
      world.desired[key] = true
      if world.loaded[key] == nil then
        queue_load(world, cx, cz, center_x, center_z)
      end
    end
  end

  for key, chunk in pairs(world.loaded) do
    if math.abs(chunk.x - center_x) > config.unload_radius or math.abs(chunk.z - center_z) > config.unload_radius then
      queue_unload(world, key, chunk)
    end
  end

  table.sort(world.pending_loads, function(a, b)
    if a.distance == b.distance then
      return a.key < b.key
    end
    return a.distance < b.distance
  end)
end

function Chunks.process_queues(world)
  for _ = 1, config.chunk_load_budget do
    local entry = table.remove(world.pending_loads, 1)
    if entry == nil then
      break
    end
    world.queued_loads[entry.key] = nil
    if world.desired[entry.key] and world.loaded[entry.key] == nil then
      Chunks.create(world, entry.x, entry.z)
    end
  end

  for _ = 1, config.chunk_unload_budget do
    local entry = table.remove(world.pending_unloads, 1)
    if entry == nil then
      break
    end
    world.queued_unloads[entry.key] = nil
    local chunk = entry.chunk
    local outside_unload_radius =
      math.abs(chunk.x - world.center_x) > config.unload_radius or math.abs(chunk.z - world.center_z) > config.unload_radius
    if world.loaded[entry.key] ~= nil and outside_unload_radius then
      Chunks.destroy(world, entry.key, entry.chunk)
    end
  end
end

return Chunks
