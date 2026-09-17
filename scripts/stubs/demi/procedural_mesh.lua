---@meta
-- Native module: require("demi.procedural_mesh"). Annotations only.
---@class ProceduralMeshBuilder
---@field clear fun(self: ProceduralMeshBuilder)
---@field reserve fun(self: ProceduralMeshBuilder, vertex_count: integer)
---@field vertex_count fun(self: ProceduralMeshBuilder): integer
---@field add_vertex fun(self: ProceduralMeshBuilder, x: number, y: number, z: number, nx: number, ny: number, nz: number, u: number, v: number)
---@field add_quad fun(self: ProceduralMeshBuilder, nx: number, ny: number, nz: number, x1: number, y1: number, z1: number, u1: number, v1: number, x2: number, y2: number, z2: number, u2: number, v2: number, x3: number, y3: number, z3: number, u3: number, v3: number, x4: number, y4: number, z4: number, u4: number, v4: number)
---@field add_voxel_blocks fun(self: ProceduralMeshBuilder, blocks: table, occupancy: table, block_tiles: table, atlas_columns: integer, occupancy_stride: integer)
---@field add_voxel_heightfield fun(self: ProceduralMeshBuilder, heights: table, top_blocks: table, fill_blocks: table, base_blocks: table, rocky_columns: table, block_tiles: table, atlas_columns: integer, chunk_size: integer, section_minimum_y: integer, section_height: integer, fill_depth: integer)

---@class ProceduralMeshService
local ProceduralMesh = {}

---@param entity_id string
---@param builder ProceduralMeshBuilder
---@param options? {texture?: string, material?: string, render_layer?: string}
---@return boolean
function ProceduralMesh.apply(entity_id, builder, options) end

---@param capacity? integer
---@return ProceduralMeshBuilder
function ProceduralMesh.create(capacity) end

return ProceduralMesh
