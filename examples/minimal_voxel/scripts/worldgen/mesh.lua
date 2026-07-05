local config = require("worldgen.config")
local terrain = require("worldgen.terrain")

local Mesh = {}

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

local function tile_uvs(tile)
  local u0 = tile / config.pack.atlas_columns
  local u1 = (tile + 1) / config.pack.atlas_columns
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
  local tiles = config.pack.blocks[block]
  local tile = tiles[face.tile] or tiles.side
  local uvs = tile_uvs(tile)
  local order = { 1, 2, 3, 1, 3, 4 }
  for _, corner_index in ipairs(order) do
    local corner = face.corners[corner_index]
    push_vertex(mesh, x + corner[1], y + corner[2], z + corner[3], face.normal, uvs[corner_index])
  end
end

function Mesh.build_chunk(world, cx, cz)
  local mesh = { vertices = {}, normals = {}, uvs = {} }
  local top_face = faces[3]
  local side_faces = { faces[1], faces[2], faces[4], faces[5] }
  for z = 0, config.chunk_size - 1 do
    for x = 0, config.chunk_size - 1 do
      local world_x = (cx * config.chunk_size) + x
      local world_z = (cz * config.chunk_size) + z
      local height = terrain.column_height(world, world_x, world_z)
      append_face(mesh, x, height, z, config.terrain.cap_block, top_face)

      for _, face in ipairs(side_faces) do
        local neighbor = face.neighbor
        local neighbor_height = terrain.column_height(world, world_x + neighbor[1], world_z + neighbor[3])
        for y = neighbor_height + 1, height do
          append_face(mesh, x, y, z, terrain.block_for_height(y, height), face)
        end
      end
    end
  end
  return mesh
end

return Mesh
