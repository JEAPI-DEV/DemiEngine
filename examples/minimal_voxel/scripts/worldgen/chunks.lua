local config = require("worldgen.config")
local mesh_builder = require("worldgen.mesh")
local terrain = require("worldgen.terrain")

local Chunks = {}
local profile_enabled = os.getenv("DEMI_PROFILE") ~= nil and os.getenv("DEMI_PROFILE") ~= "0"

local function queue_count(world, queue_name, head_name)
  return math.max(0, #world[queue_name] - world[head_name] + 1)
end

local function compact_queue(world, queue_name, head_name)
  local queue = world[queue_name]
  local head = world[head_name]
  if head <= 1 then
    return
  end
  local remaining = queue_count(world, queue_name, head_name)
  for index = 1, remaining do
    queue[index] = queue[head + index - 1]
  end
  for index = remaining + 1, #queue do
    queue[index] = nil
  end
  world[head_name] = 1
end

local function pop_queue(world, queue_name, head_name)
  local queue = world[queue_name]
  local head = world[head_name]
  local entry = queue[head]
  if entry == nil then
    return nil
  end
  queue[head] = false
  world[head_name] = head + 1
  if world[head_name] > 64 and world[head_name] > (#queue / 2) then
    compact_queue(world, queue_name, head_name)
  end
  return entry
end

function Chunks.pending_count(world)
  return queue_count(world, "pending_loads", "pending_load_head")
    + queue_count(world, "pending_unloads", "pending_unload_head")
    + queue_count(world, "pending_section_rebuilds", "pending_section_rebuild_head")
end

local function create_section(world, cx, section_y, cz, mesh)
  local section_min_y = terrain.section_bounds(section_y)
  local section_id = terrain.section_id(cx, section_y, cz)
  Entity.create(section_id, {
    name = string.format("Terrain Chunk %d %d Section %d", cx, cz, section_y),
    components = {
      Transform3D = {
        position = { cx * config.chunk_size, section_min_y, cz * config.chunk_size },
        rotation = { 0.0, 0.0, 0.0 },
        scale = { 1.0, 1.0, 1.0 },
      },
    },
  })
  if profile_enabled then
    Profile.scope("Voxel.section_mesh_apply_create", function()
      ProceduralMesh.apply(section_id, mesh, config.pack.texture)
    end)
  else
    ProceduralMesh.apply(section_id, mesh, config.pack.texture)
  end
end

local function update_section(cx, section_y, cz, mesh)
  if profile_enabled then
    Profile.scope("Voxel.section_mesh_apply_update", function()
      ProceduralMesh.apply(terrain.section_id(cx, section_y, cz), mesh, config.pack.texture)
    end)
  else
    ProceduralMesh.apply(terrain.section_id(cx, section_y, cz), mesh, config.pack.texture)
  end
end

local function destroy_section(cx, section_y, cz)
  Entity.destroy(terrain.section_id(cx, section_y, cz))
end

function Chunks.mark_section_dirty(world, cx, section_y, cz)
  if section_y < 0 or section_y >= terrain.section_count() then
    return
  end
  local chunk = world.loaded[terrain.chunk_key(cx, cz)]
  if chunk == nil then
    return
  end
  local key = terrain.section_key(cx, section_y, cz)
  if world.queued_section_rebuilds[key] then
    return
  end
  world.queued_section_rebuilds[key] = true
  world.pending_section_rebuilds[#world.pending_section_rebuilds + 1] = {
    key = key,
    x = cx,
    y = section_y,
    z = cz,
  }
end

function Chunks.mark_chunk_dirty(world, cx, cz)
  local min_section_y, max_section_y = terrain.active_section_range(world, cx, cz)
  if min_section_y == nil then
    return
  end
  for section_y = min_section_y, max_section_y do
    Chunks.mark_section_dirty(world, cx, section_y, cz)
  end
end

function Chunks.create(world, cx, cz)
  world.loaded[terrain.chunk_key(cx, cz)] = { x = cx, z = cz, sections = {} }
  world.loaded_count = (world.loaded_count or 0) + 1
  Chunks.mark_chunk_dirty(world, cx, cz)
end

function Chunks.destroy(world, key, chunk)
  for section_y in pairs(chunk.sections or {}) do
    destroy_section(chunk.x, section_y, chunk.z)
  end
  world.loaded[key] = nil
  world.loaded_count = math.max(0, (world.loaded_count or 1) - 1)
end

function Chunks.rebuild_section(world, cx, section_y, cz)
  local chunk = world.loaded[terrain.chunk_key(cx, cz)]
  if chunk == nil then
    return
  end

  local mesh = nil
  if profile_enabled then
    Profile.scope("Voxel.section_mesh_build", function()
      mesh = mesh_builder.build_section(world, cx, section_y, cz)
    end)
  else
    mesh = mesh_builder.build_section(world, cx, section_y, cz)
  end
  if mesh:vertex_count() == 0 then
    if chunk.sections[section_y] then
      destroy_section(cx, section_y, cz)
      chunk.sections[section_y] = nil
    end
    return
  end
  if chunk.sections[section_y] then
    update_section(cx, section_y, cz, mesh)
  else
    create_section(world, cx, section_y, cz, mesh)
    chunk.sections[section_y] = true
  end
end

function Chunks.rebuild(world, cx, cz)
  Chunks.mark_chunk_dirty(world, cx, cz)
end

function Chunks.rebuild_column(world, world_x, world_z)
  local cx = terrain.chunk_coord(world_x)
  local cz = terrain.chunk_coord(world_z)
  Chunks.mark_chunk_dirty(world, cx, cz)
  if terrain.local_coord(world_x) == 0 then
    Chunks.mark_chunk_dirty(world, cx - 1, cz)
  elseif terrain.local_coord(world_x) == config.chunk_size - 1 then
    Chunks.mark_chunk_dirty(world, cx + 1, cz)
  end
  if terrain.local_coord(world_z) == 0 then
    Chunks.mark_chunk_dirty(world, cx, cz - 1)
  elseif terrain.local_coord(world_z) == config.chunk_size - 1 then
    Chunks.mark_chunk_dirty(world, cx, cz + 1)
  end
end

function Chunks.rebuild_block(world, world_x, y, world_z)
  local cx = terrain.chunk_coord(world_x)
  local cz = terrain.chunk_coord(world_z)
  local section_y = terrain.section_coord(y)
  Chunks.mark_section_dirty(world, cx, section_y, cz)
  if terrain.local_section_coord(y) == 0 then
    Chunks.mark_section_dirty(world, cx, section_y - 1, cz)
  elseif terrain.local_section_coord(y) == config.section_height - 1 then
    Chunks.mark_section_dirty(world, cx, section_y + 1, cz)
  end
  if terrain.local_coord(world_x) == 0 then
    Chunks.mark_section_dirty(world, cx - 1, section_y, cz)
  elseif terrain.local_coord(world_x) == config.chunk_size - 1 then
    Chunks.mark_section_dirty(world, cx + 1, section_y, cz)
  end
  if terrain.local_coord(world_z) == 0 then
    Chunks.mark_section_dirty(world, cx, section_y, cz - 1)
  elseif terrain.local_coord(world_z) == config.chunk_size - 1 then
    Chunks.mark_section_dirty(world, cx, section_y, cz + 1)
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

  compact_queue(world, "pending_loads", "pending_load_head")
  table.sort(world.pending_loads, function(a, b)
    if a.distance == b.distance then
      return a.key < b.key
    end
    return a.distance < b.distance
  end)
end

function Chunks.process_queues(world)
  for _ = 1, config.chunk_unload_budget do
    local entry = pop_queue(world, "pending_unloads", "pending_unload_head")
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

  local rebuilt_sections = 0
  for _ = 1, config.section_rebuild_budget do
    local entry = pop_queue(world, "pending_section_rebuilds", "pending_section_rebuild_head")
    if entry == nil then
      break
    end
    world.queued_section_rebuilds[entry.key] = nil
    Chunks.rebuild_section(world, entry.x, entry.y, entry.z)
    rebuilt_sections = rebuilt_sections + 1
  end

  if rebuilt_sections > 0 then
    return
  end

  for _ = 1, config.chunk_load_budget do
    local entry = pop_queue(world, "pending_loads", "pending_load_head")
    if entry == nil then
      break
    end
    world.queued_loads[entry.key] = nil
    if world.desired[entry.key] and world.loaded[entry.key] == nil then
      Chunks.create(world, entry.x, entry.z)
    end
  end
end

return Chunks
