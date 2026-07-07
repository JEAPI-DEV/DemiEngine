local config = require("worldgen.config")
local terrain = require("worldgen.terrain")

local Mesh = {}
local atlas_tile_width = 1.0 / config.pack.atlas_columns
local block_tiles = config.pack.blocks
local block_definitions = config.blocks
local profile_enabled = os.getenv("DEMI_PROFILE") ~= nil and os.getenv("DEMI_PROFILE") ~= "0"

local function profile_scope(name, callback)
  if profile_enabled then
    return Profile.scope(name, callback)
  end
  return callback()
end

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

local side_faces = { faces[1], faces[2], faces[5], faces[6] }

local function append_face(mesh, x, y, z, block, face)
  local tiles = block_tiles[block]
  if tiles == nil then
    return
  end
  local tile = tiles[face.tile] or tiles.side
  local u0 = tile * atlas_tile_width
  local u1 = u0 + atlas_tile_width
  local c1 = face.corners[1]
  local c2 = face.corners[2]
  local c3 = face.corners[3]
  local c4 = face.corners[4]
  mesh:add_quad(
    face.normal[1], face.normal[2], face.normal[3],
    x + c1[1], y + c1[2], z + c1[3], u0, 1.0,
    x + c2[1], y + c2[2], z + c2[3], u1, 1.0,
    x + c3[1], y + c3[2], z + c3[3], u1, 0.0,
    x + c4[1], y + c4[2], z + c4[3], u0, 0.0
  )
end

local cross_faces = {
  {
    normal = { 0.707, 0.0, 0.707 },
    corners = {
      { 0.08, 0.0, 0.08 },
      { 0.92, 0.0, 0.92 },
      { 0.92, 0.9, 0.92 },
      { 0.08, 0.9, 0.08 },
    },
  },
  {
    normal = { -0.707, 0.0, 0.707 },
    corners = {
      { 0.92, 0.0, 0.08 },
      { 0.08, 0.0, 0.92 },
      { 0.08, 0.9, 0.92 },
      { 0.92, 0.9, 0.08 },
    },
  },
}

local function append_cross(mesh, x, y, z, block)
  local tiles = block_tiles[block]
  if tiles == nil then
    return
  end
  local tile = tiles.side or tiles.top
  local u0 = tile * atlas_tile_width
  local u1 = u0 + atlas_tile_width
  for _, face in ipairs(cross_faces) do
    local c1 = face.corners[1]
    local c2 = face.corners[2]
    local c3 = face.corners[3]
    local c4 = face.corners[4]
    mesh:add_quad(
      face.normal[1], face.normal[2], face.normal[3],
      x + c1[1], y + c1[2], z + c1[3], u0, 1.0,
      x + c2[1], y + c2[2], z + c2[3], u1, 1.0,
      x + c3[1], y + c3[2], z + c3[3], u1, 0.0,
      x + c4[1], y + c4[2], z + c4[3], u0, 0.0
    )
    mesh:add_quad(
      -face.normal[1], -face.normal[2], -face.normal[3],
      x + c4[1], y + c4[2], z + c4[3], u0, 0.0,
      x + c3[1], y + c3[2], z + c3[3], u1, 0.0,
      x + c2[1], y + c2[2], z + c2[3], u1, 1.0,
      x + c1[1], y + c1[2], z + c1[3], u0, 1.0
    )
  end
end

local function append_block(world, mesh, cx, section_y, cz, world_x, y, world_z, block)
  local section_min_y = terrain.section_bounds(section_y)
  local local_x = world_x - (cx * config.chunk_size)
  local local_y = y - section_min_y
  local local_z = world_z - (cz * config.chunk_size)
  if block_definitions[block] and block_definitions[block].render == "cross" then
    append_cross(mesh, local_x, local_y, local_z, block)
    return
  end
  for _, face in ipairs(faces) do
    local neighbor = face.neighbor
    if terrain.block_at(world, world_x + neighbor[1], y + neighbor[2], world_z + neighbor[3]) == 0 then
      append_face(mesh, local_x, local_y, local_z, block, face)
    end
  end
end

local function feature_occupancy_key(local_x, y, local_z)
  local stride = config.chunk_size + 2
  return (y * stride * stride) + ((local_z + 1) * stride) + local_x + 1
end

local function append_generated_feature_cross(mesh, cx, section_y, cz, world_x, y, world_z, block)
  local section_min_y = terrain.section_bounds(section_y)
  local local_x = world_x - (cx * config.chunk_size)
  local local_y = y - section_min_y
  local local_z = world_z - (cz * config.chunk_size)
  append_cross(mesh, local_x, local_y, local_z, block)
end

local function append_removed_block_neighbors(world, mesh, cx, section_y, cz, removed_x, removed_y, removed_z)
  for _, face in ipairs(faces) do
    local neighbor = face.neighbor
    local world_x = removed_x - neighbor[1]
    local y = removed_y - neighbor[2]
    local world_z = removed_z - neighbor[3]
    if
      y >= 0
      and terrain.chunk_coord(world_x) == cx
      and terrain.chunk_coord(world_z) == cz
      and terrain.section_coord(y) == section_y
    then
      local block = terrain.block_at(world, world_x, y, world_z)
      if block ~= 0 then
        local section_min_y = terrain.section_bounds(section_y)
        append_face(
          mesh,
          world_x - (cx * config.chunk_size),
          y - section_min_y,
          world_z - (cz * config.chunk_size),
          block,
          face
        )
      end
    end
  end
end

local function terrain_block_for_column_y(y, height, biome, rocky)
  local surface = biome or config.terrain
  if y > height then
    return 0
  end
  if y == height then
    return rocky and config.terrain.base_block or surface.cap_block
  end
  if y == height - 1 and rocky then
    return config.terrain.base_block
  end
  if y >= height - config.terrain.fill_depth then
    return surface.fill_block
  end
  return surface.base_block
end

function Mesh.build_section(world, cx, section_y, cz)
  local mesh = ProceduralMesh.create(4096)
  local section_min_y, section_max_y = terrain.section_bounds(section_y)
  local top_face = faces[3]
  local has_overrides = world.block_overrides ~= nil and next(world.block_overrides) ~= nil
  profile_scope("Voxel.mesh_terrain", function()
    for z = 0, config.chunk_size - 1 do
      for x = 0, config.chunk_size - 1 do
        local world_x = (cx * config.chunk_size) + x
        local world_z = (cz * config.chunk_size) + z
        local height = nil
        local top_block = nil
        local biome = nil
        local rocky = false
        if has_overrides then
          height, top_block = terrain.surface_height(world, world_x, world_z)
        else
          height = terrain.column_height(world, world_x, world_z)
          biome = terrain.biome_at(world, world_x, world_z)
          rocky = terrain.rocky_surface_at(world, world_x, world_z, height, biome)
          top_block = terrain_block_for_column_y(height, height, biome, rocky)
        end
        if top_block ~= 0 and height >= section_min_y and height <= section_max_y then
          append_face(mesh, x, height - section_min_y, z, top_block, top_face)
        end

        for _, face in ipairs(side_faces) do
          local neighbor = face.neighbor
          local neighbor_height = nil
          if has_overrides then
            neighbor_height = terrain.surface_height(world, world_x + neighbor[1], world_z + neighbor[3])
          else
            neighbor_height = terrain.column_height(world, world_x + neighbor[1], world_z + neighbor[3])
          end
          local min_y = math.max(neighbor_height + 1, section_min_y)
          local max_y = math.min(height, section_max_y)
          for y = min_y, max_y do
            local block = nil
            local exposed = true
            if has_overrides then
              block = terrain.terrain_block_at(world, world_x, y, world_z)
              exposed = terrain.terrain_block_at(world, world_x + neighbor[1], y, world_z + neighbor[3]) == 0
            else
              block = terrain_block_for_column_y(y, height, biome, rocky)
            end
            if block ~= 0 and exposed then
              append_face(mesh, x, y - section_min_y, z, block, face)
            end
          end
        end
      end
    end
  end)

  profile_scope("Voxel.mesh_features", function()
    if has_overrides then
      terrain.for_feature_blocks_in_chunk(world, cx, cz, function(world_x, y, world_z, block)
        if y >= section_min_y and y <= section_max_y and terrain.block_at(world, world_x, y, world_z) == block then
          append_block(world, mesh, cx, section_y, cz, world_x, y, world_z, block)
        end
      end)
    else
      local feature_entries = terrain.feature_blocks_in_section(world, cx, section_y, cz)
      local feature_blocks = {}
      local solid_features = {}
      local chunk_min_x = cx * config.chunk_size
      local chunk_min_z = cz * config.chunk_size
      for _, entry in ipairs(feature_entries) do
        local local_x = entry.x - chunk_min_x
        local local_y = entry.y - section_min_y
        local local_z = entry.z - chunk_min_z
        feature_blocks[feature_occupancy_key(local_x, local_y, local_z)] = true
        if not (block_definitions[entry.block] and block_definitions[entry.block].render == "cross") then
          solid_features[#solid_features + 1] = { x = local_x, y = local_y, z = local_z, block = entry.block }
        end
      end
      local previous_features = terrain.feature_blocks_in_section(world, cx, section_y - 1, cz)
      for _, entry in ipairs(previous_features) do
        if entry.y == section_min_y - 1 then
          feature_blocks[feature_occupancy_key(entry.x - chunk_min_x, -1, entry.z - chunk_min_z)] = true
        end
      end
      local next_features = terrain.feature_blocks_in_section(world, cx, section_y + 1, cz)
      for _, entry in ipairs(next_features) do
        if entry.y == section_max_y + 1 then
          feature_blocks[feature_occupancy_key(entry.x - chunk_min_x, config.section_height, entry.z - chunk_min_z)] = true
        end
      end
      for _, entry in ipairs(feature_entries) do
        if entry.y >= section_min_y and entry.y <= section_max_y then
          if block_definitions[entry.block] and block_definitions[entry.block].render == "cross" then
            append_generated_feature_cross(mesh, cx, section_y, cz, entry.x, entry.y, entry.z, entry.block)
          end
        end
      end
      for _, entry in ipairs(solid_features) do
        for _, face in ipairs(faces) do
          local neighbor = face.neighbor
          local world_x = chunk_min_x + entry.x + neighbor[1]
          local world_y = section_min_y + entry.y + neighbor[2]
          local world_z = chunk_min_z + entry.z + neighbor[3]
          if world_y >= 0 and world_y <= terrain.column_height(world, world_x, world_z) then
            feature_blocks[feature_occupancy_key(entry.x + neighbor[1], entry.y + neighbor[2], entry.z + neighbor[3])] = true
          end
        end
      end
      mesh:add_voxel_blocks(solid_features, feature_blocks, block_tiles, config.pack.atlas_columns, config.chunk_size + 2)
    end
  end)

  profile_scope("Voxel.mesh_overrides", function()
    for key, block in pairs(world.block_overrides or {}) do
      local world_x, y, world_z = terrain.parse_block_key(key)
      if block ~= 0 then
        if
          world_x ~= nil
          and y >= section_min_y
          and y <= section_max_y
          and terrain.chunk_coord(world_x) == cx
          and terrain.chunk_coord(world_z) == cz
        then
          append_block(world, mesh, cx, section_y, cz, world_x, y, world_z, block)
        end
      elseif world_x ~= nil then
        append_removed_block_neighbors(world, mesh, cx, section_y, cz, world_x, y, world_z)
      end
    end
  end)

  return mesh
end

function Mesh.build_chunk(world, cx, cz)
  local min_section_y, max_section_y = terrain.active_section_range(world, cx, cz)
  if min_section_y == nil then
    return ProceduralMesh.create(0)
  end
  return Mesh.build_section(world, cx, min_section_y, cz)
end

return Mesh
