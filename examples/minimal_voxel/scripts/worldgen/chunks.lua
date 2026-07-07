local config = require("worldgen.config")
local mesh_builder = require("worldgen.mesh")
local terrain = require("worldgen.terrain")

local Chunks = {}
local profile_enabled = os.getenv("DEMI_PROFILE") ~= nil and os.getenv("DEMI_PROFILE") ~= "0"

local function queue_count(world, queue_name, head_name)
  local count = 0
  local queue = world[queue_name]
  for index = world[head_name], #queue do
    if queue[index] ~= false and queue[index] ~= nil then
      count = count + 1
    end
  end
  return count
end

local function compact_queue(world, queue_name, head_name)
  local queue = world[queue_name]
  local head = world[head_name]
  if head <= 1 then
    return
  end
  local write_index = 1
  for read_index = head, #queue do
    local entry = queue[read_index]
    if entry ~= false and entry ~= nil then
      queue[write_index] = entry
      write_index = write_index + 1
    end
  end
  for index = write_index, #queue do
    queue[index] = nil
  end
  world[head_name] = 1
end

local function prune_queue(world, queue_name, head_name, keep_entry, drop_entry)
  compact_queue(world, queue_name, head_name)
  local queue = world[queue_name]
  local write_index = 1
  for read_index = 1, #queue do
    local entry = queue[read_index]
    if entry ~= false and entry ~= nil and keep_entry(entry) then
      queue[write_index] = entry
      write_index = write_index + 1
    else
      drop_entry(entry)
    end
  end
  for index = write_index, #queue do
    queue[index] = nil
  end
  world[head_name] = 1
end

local function pop_queue(world, queue_name, head_name)
  local queue = world[queue_name]
  local head = world[head_name]
  while queue[head] == false do
    head = head + 1
  end
  local entry = queue[head]
  if entry == nil then
    world[head_name] = head
    return nil
  end
  queue[head] = false
  world[head_name] = head + 1
  if world[head_name] > 64 and world[head_name] > (#queue / 2) then
    compact_queue(world, queue_name, head_name)
  end
  return entry
end

local function chunk_distance(cx, cz, center_x, center_z)
  return ((cx - center_x) * (cx - center_x)) + ((cz - center_z) * (cz - center_z))
end

local function section_distance(cx, section_y, cz, center_x, center_y, center_z)
  return chunk_distance(cx, cz, center_x, center_z) + ((section_y - center_y) * (section_y - center_y))
end

local function outside_radius(cx, cz, center_x, center_z, radius)
  return math.abs(cx - center_x) > radius or math.abs(cz - center_z) > radius
end

local function outside_vertical_radius(section_y, center_y, radius)
  return math.abs(section_y - center_y) > radius
end

local function desired_section(world, cx, section_y, cz)
  return
    world.desired ~= nil
    and world.desired[terrain.chunk_key(cx, cz)] == true
    and world.center_y ~= nil
    and not outside_vertical_radius(section_y, world.center_y, config.vertical_load_radius)
end

local function reprioritize_queue(world, queue_name, head_name)
  compact_queue(world, queue_name, head_name)
  local queue = world[queue_name]
  for _, entry in ipairs(queue) do
    if entry.y ~= nil then
      entry.distance = section_distance(entry.x, entry.y, entry.z, world.center_x or entry.x, world.center_y or entry.y, world.center_z or entry.z)
    else
      entry.distance = chunk_distance(entry.x, entry.z, world.center_x or entry.x, world.center_z or entry.z)
    end
  end
  table.sort(queue, function(a, b)
    if a.distance == b.distance then
      if a.y ~= nil and b.y ~= nil and a.y ~= b.y then
        return a.y < b.y
      end
      return a.key < b.key
    end
    return a.distance < b.distance
  end)
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
  world.loaded_section_count = (world.loaded_section_count or 0) + 1
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

function Chunks.mark_section_dirty(world, cx, section_y, cz)
  return Chunks.mark_section_dirty_priority(world, cx, section_y, cz, false)
end

function Chunks.mark_section_dirty_priority(world, cx, section_y, cz, priority)
  if section_y < 0 then
    return
  end
  if world.desired_sections ~= nil and not desired_section(world, cx, section_y, cz) then
    return
  end
  local chunk = world.loaded[terrain.chunk_key(cx, cz)]
  if chunk == nil then
    return
  end
  local key = terrain.section_key(cx, section_y, cz)
  chunk.empty_sections = chunk.empty_sections or {}
  chunk.empty_sections[section_y] = nil
  world.dirty_sections = world.dirty_sections or {}
  world.dirty_sections[key] = { key = key, x = cx, y = section_y, z = cz }
  if world.queued_section_rebuilds[key] then
    if priority then
      for index = world.pending_section_rebuild_head, #world.pending_section_rebuilds do
        local entry = world.pending_section_rebuilds[index]
        if entry ~= false and entry.key == key then
          world.pending_section_rebuilds[index] = false
          break
        end
      end
    else
      return
    end
  end
  local entry = {
    key = key,
    x = cx,
    y = section_y,
    z = cz,
    distance = section_distance(cx, section_y, cz, world.center_x or cx, world.center_y or section_y, world.center_z or cz),
  }
  world.queued_section_rebuilds[key] = true
  if priority then
    table.insert(world.pending_section_rebuilds, world.pending_section_rebuild_head, entry)
    return
  end
  world.pending_section_rebuilds[#world.pending_section_rebuilds + 1] = entry
end

function Chunks.mark_chunk_dirty(world, cx, cz)
  local min_section_y, max_section_y = terrain.active_section_range(world, cx, cz)
  if min_section_y == nil then
    return
  end
  if world.center_y ~= nil then
    min_section_y = math.max(min_section_y, world.center_y - config.vertical_load_radius)
    max_section_y = math.min(max_section_y, world.center_y + config.vertical_load_radius)
  end
  for section_y = min_section_y, max_section_y do
    Chunks.mark_section_dirty(world, cx, section_y, cz)
  end
end

local function destroy_section(world, chunk, section_y)
  Entity.destroy(terrain.section_id(chunk.x, section_y, chunk.z))
  chunk.sections[section_y] = nil
  world.loaded_section_count = math.max(0, (world.loaded_section_count or 1) - 1)
  local section_key = terrain.section_key(chunk.x, section_y, chunk.z)
  world.queued_section_rebuilds[section_key] = nil
  if world.dirty_sections ~= nil then
    world.dirty_sections[section_key] = nil
  end
end

local function queue_missing_desired_sections(world, cx, cz)
  local chunk = world.loaded[terrain.chunk_key(cx, cz)]
  if chunk == nil or world.center_y == nil then
    return
  end
  for section_y = math.max(0, world.center_y - config.vertical_load_radius), world.center_y + config.vertical_load_radius do
    local section_key = terrain.section_key(cx, section_y, cz)
    local known_empty = chunk.empty_sections ~= nil and chunk.empty_sections[section_y]
    if desired_section(world, cx, section_y, cz) and not chunk.sections[section_y] and not known_empty and not world.queued_section_rebuilds[section_key] then
      Chunks.mark_section_dirty(world, cx, section_y, cz)
    end
  end
end

function Chunks.create(world, cx, cz)
  world.loaded[terrain.chunk_key(cx, cz)] = { x = cx, z = cz, sections = {}, empty_sections = {} }
  world.loaded_count = (world.loaded_count or 0) + 1
  if profile_enabled then
    Profile.scope("Voxel.chunk_features_generate", function()
      terrain.feature_blocks_in_chunk(world, cx, cz)
    end)
  else
    terrain.feature_blocks_in_chunk(world, cx, cz)
  end
  queue_missing_desired_sections(world, cx, cz)
end

function Chunks.destroy(world, key, chunk)
  local section_ids = {}
  for section_y in pairs(chunk.sections or {}) do
    section_ids[#section_ids + 1] = terrain.section_id(chunk.x, section_y, chunk.z)
  end
  if #section_ids > 0 then
    Entity.destroy_many(section_ids)
    world.loaded_section_count = math.max(0, (world.loaded_section_count or 0) - #section_ids)
  end
  for section_y in pairs(chunk.sections or {}) do
    local section_key = terrain.section_key(chunk.x, section_y, chunk.z)
    world.queued_section_rebuilds[section_key] = nil
    if world.dirty_sections ~= nil then
      world.dirty_sections[section_key] = nil
    end
  end
  world.loaded[key] = nil
  world.loaded_count = math.max(0, (world.loaded_count or 1) - 1)
end

function Chunks.rebuild_section(world, cx, section_y, cz)
  local section_key = terrain.section_key(cx, section_y, cz)
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
      destroy_section(world, chunk, section_y)
    end
    chunk.empty_sections = chunk.empty_sections or {}
    chunk.empty_sections[section_y] = true
    if world.dirty_sections ~= nil then
      world.dirty_sections[section_key] = nil
    end
    return
  end
  if chunk.sections[section_y] then
    update_section(cx, section_y, cz, mesh)
  else
    create_section(world, cx, section_y, cz, mesh)
    chunk.sections[section_y] = true
  end
  if chunk.empty_sections ~= nil then
    chunk.empty_sections[section_y] = nil
  end
  if world.dirty_sections ~= nil then
    world.dirty_sections[section_key] = nil
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
  if terrain.local_section_coord(y) == 0 then
    Chunks.mark_section_dirty_priority(world, cx, section_y - 1, cz, true)
  end
  if terrain.local_section_coord(y) == config.section_height - 1 then
    Chunks.mark_section_dirty_priority(world, cx, section_y + 1, cz, true)
  end
  if terrain.local_coord(world_x) == 0 then
    Chunks.mark_section_dirty_priority(world, cx - 1, section_y, cz, true)
  elseif terrain.local_coord(world_x) == config.chunk_size - 1 then
    Chunks.mark_section_dirty_priority(world, cx + 1, section_y, cz, true)
  end
  if terrain.local_coord(world_z) == 0 then
    Chunks.mark_section_dirty_priority(world, cx, section_y, cz - 1, true)
  elseif terrain.local_coord(world_z) == config.chunk_size - 1 then
    Chunks.mark_section_dirty_priority(world, cx, section_y, cz + 1, true)
  end
  Chunks.mark_section_dirty_priority(world, cx, section_y, cz, true)
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
    distance = chunk_distance(cx, cz, center_x, center_z),
  }
end

local function queue_unload(world, key, chunk)
  if world.queued_unloads[key] then
    return
  end
  world.queued_unloads[key] = true
  world.pending_unloads[#world.pending_unloads + 1] = { key = key, chunk = chunk }
end

local function requeue_dirty_desired_sections(world, center_x, center_z)
  if world.dirty_sections == nil then
    return
  end
  for key, dirty in pairs(world.dirty_sections) do
    local chunk_key = terrain.chunk_key(dirty.x, dirty.z)
    if world.desired[chunk_key] and desired_section(world, dirty.x, dirty.y, dirty.z) and world.loaded[chunk_key] ~= nil and not world.queued_section_rebuilds[key] then
      world.queued_section_rebuilds[key] = true
      world.pending_section_rebuilds[#world.pending_section_rebuilds + 1] = {
        key = key,
        x = dirty.x,
        y = dirty.y,
        z = dirty.z,
        distance = section_distance(dirty.x, dirty.y, dirty.z, center_x, world.center_y or dirty.y, center_z),
      }
    end
  end
end

local function unload_out_of_range_sections(world)
  if world.center_y == nil then
    return
  end
  for _, chunk in pairs(world.loaded) do
    for section_y in pairs(chunk.sections or {}) do
      if outside_vertical_radius(section_y, world.center_y, config.vertical_unload_radius) then
        destroy_section(world, chunk, section_y)
      end
    end
  end
end

function Chunks.sync(world, center_x, center_y, center_z)
  world.desired = {}
  for cz = center_z - config.load_radius, center_z + config.load_radius do
    for cx = center_x - config.load_radius, center_x + config.load_radius do
      local key = terrain.chunk_key(cx, cz)
      world.desired[key] = true
      if world.loaded[key] == nil then
        queue_load(world, cx, cz, center_x, center_z)
      else
        queue_missing_desired_sections(world, cx, cz)
      end
    end
  end
  unload_out_of_range_sections(world)

  for key, chunk in pairs(world.loaded) do
    if outside_radius(chunk.x, chunk.z, center_x, center_z, config.unload_radius) then
      queue_unload(world, key, chunk)
    end
  end

  prune_queue(
    world,
    "pending_loads",
    "pending_load_head",
    function(entry)
      return world.desired[entry.key] and world.loaded[entry.key] == nil
    end,
    function(entry)
      if entry ~= false and entry ~= nil then
        world.queued_loads[entry.key] = nil
      end
    end
  )
  prune_queue(
    world,
    "pending_unloads",
    "pending_unload_head",
    function(entry)
      local chunk = entry.chunk
      return world.loaded[entry.key] ~= nil and outside_radius(chunk.x, chunk.z, center_x, center_z, config.unload_radius)
    end,
    function(entry)
      if entry ~= false and entry ~= nil then
        world.queued_unloads[entry.key] = nil
      end
    end
  )
  prune_queue(
    world,
    "pending_section_rebuilds",
    "pending_section_rebuild_head",
    function(entry)
      return desired_section(world, entry.x, entry.y, entry.z) and world.loaded[terrain.chunk_key(entry.x, entry.z)] ~= nil
    end,
    function(entry)
      if entry ~= false and entry ~= nil then
        world.queued_section_rebuilds[entry.key] = nil
      end
    end
  )
  requeue_dirty_desired_sections(world, center_x, center_z)

  reprioritize_queue(world, "pending_loads", "pending_load_head")
  reprioritize_queue(world, "pending_section_rebuilds", "pending_section_rebuild_head")
end

local function process_loads(world, budget_spent)
  for _ = 1, config.chunk_load_budget do
    if budget_spent() then
      return 0
    end
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

function Chunks.process_queues(world)
  local started_at = os.clock()
  local function budget_spent()
    return ((os.clock() - started_at) * 1000.0) >= config.chunk_process_budget_ms
  end

  for _ = 1, config.chunk_unload_budget do
    if budget_spent() then
      return
    end
    local entry = pop_queue(world, "pending_unloads", "pending_unload_head")
    if entry == nil then
      break
    end
    world.queued_unloads[entry.key] = nil
    local chunk = entry.chunk
    if world.loaded[entry.key] ~= nil and outside_radius(chunk.x, chunk.z, world.center_x, world.center_z, config.unload_radius) then
      Chunks.destroy(world, entry.key, entry.chunk)
    end
  end

  local rebuilt_sections = 0
  for _ = 1, config.section_rebuild_budget do
    if budget_spent() then
      return
    end
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

  if rebuilt_sections == 0 then
    process_loads(world, budget_spent)
  end
end

return Chunks
