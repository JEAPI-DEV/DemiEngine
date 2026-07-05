local config = require("worldgen.config")
local terrain = require("worldgen.terrain")

local Mesh = {}
local atlas_tile_width = 1.0 / config.pack.atlas_columns
local block_tiles = config.pack.blocks

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

local function append_block(world, mesh, cx, section_y, cz, world_x, y, world_z, block)
  local section_min_y = terrain.section_bounds(section_y)
  local local_x = world_x - (cx * config.chunk_size)
  local local_y = y - section_min_y
  local local_z = world_z - (cz * config.chunk_size)
  for _, face in ipairs(faces) do
    local neighbor = face.neighbor
    if terrain.block_at(world, world_x + neighbor[1], y + neighbor[2], world_z + neighbor[3]) == 0 then
      append_face(mesh, local_x, local_y, local_z, block, face)
    end
  end
end

local function append_removed_block_neighbors(world, mesh, cx, section_y, cz, removed_x, removed_y, removed_z)
  for _, face in ipairs(faces) do
    local neighbor = face.neighbor
    local world_x = removed_x - neighbor[1]
    local y = removed_y - neighbor[2]
    local world_z = removed_z - neighbor[3]
    if
      y >= 0
      and y < config.chunk_height
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

function Mesh.build_section(world, cx, section_y, cz)
  local mesh = ProceduralMesh.create(4096)
  local section_min_y, section_max_y = terrain.section_bounds(section_y)
  local top_face = faces[3]
  local side_faces = { faces[1], faces[2], faces[5], faces[6] }
  for z = 0, config.chunk_size - 1 do
    for x = 0, config.chunk_size - 1 do
      local world_x = (cx * config.chunk_size) + x
      local world_z = (cz * config.chunk_size) + z
      local height, top_block = terrain.surface_height(world, world_x, world_z)
      if top_block ~= 0 and height >= section_min_y and height <= section_max_y then
        append_face(mesh, x, height - section_min_y, z, top_block, top_face)
      end

      for _, face in ipairs(side_faces) do
        local neighbor = face.neighbor
        local neighbor_height = terrain.surface_height(world, world_x + neighbor[1], world_z + neighbor[3])
        local min_y = math.max(neighbor_height + 1, section_min_y)
        local max_y = math.min(height, section_max_y)
        for y = min_y, max_y do
          local block = terrain.terrain_block_at(world, world_x, y, world_z)
          if block ~= 0 and terrain.terrain_block_at(world, world_x + neighbor[1], y, world_z + neighbor[3]) == 0 then
            append_face(mesh, x, y - section_min_y, z, block, face)
          end
        end
      end
    end
  end

  terrain.for_feature_blocks_in_chunk(world, cx, cz, function(world_x, y, world_z, block)
    if y >= section_min_y and y <= section_max_y and terrain.block_at(world, world_x, y, world_z) == block then
      append_block(world, mesh, cx, section_y, cz, world_x, y, world_z, block)
    end
  end)

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
