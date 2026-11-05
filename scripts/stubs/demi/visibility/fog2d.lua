---@meta
-- Native module: require("demi.visibility.fog2d"). Annotations only.
local Fog2D = {}
---Configure scene-local fog. Origin is the lower-left map corner. Maximum 256x256 cells.
---Invalid input returns false and preserves the existing mask. RGB and opacity are 0..1.
---@param width integer
---@param height integer
---@param cell_size number
---@param origin_x number
---@param origin_y number
---@param r number
---@param g number
---@param b number
---@param memory_opacity number
---@return boolean
function Fog2D.configure(width, height, cell_size, origin_x, origin_y, r, g, b, memory_opacity) end
---Remember a zero-based cell. Published on the next rendered frame; persist discovery in gameplay saves.
---@param x integer
---@param y integer
function Fog2D.reveal(x, y) end
---Recompute smooth sight from native collider rays. Disabled colliders are ignored.
---World-space radius must be >0 and <=256; feather must be >0 and affects the range edge only.
---This changes rendering only: gameplay must still enforce actor visibility and targeting.
---@param x number
---@param y number
---@param radius number
---@param feather number
---@param occluder_layer string
---@return boolean
function Fog2D.update(x, y, radius, feather, occluder_layer) end
---Release this world's fog mask. Also cleared when its World is replaced.
function Fog2D.clear() end
return Fog2D
