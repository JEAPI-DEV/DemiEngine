---@meta
-- Native module: require("demi.voxel.world"). Annotations only.
---@class VoxelWorldHandle
---@field clear fun(self: VoxelWorldHandle)
---@field set_section fun(self: VoxelWorldHandle, cx: integer, section_y: integer, cz: integer, blocks: table)
---@field erase_section fun(self: VoxelWorldHandle, cx: integer, section_y: integer, cz: integer)
---@field build_section_mesh fun(self: VoxelWorldHandle, cx: integer, section_y: integer, cz: integer, block_tiles: table, atlas_columns: integer): ProceduralMeshBuilder

---@class VoxelWorldService
local VoxelWorld = {}
---@param chunk_size integer
---@param section_height integer
---@return VoxelWorldHandle
function VoxelWorld.create(chunk_size, section_height) end

return VoxelWorld
