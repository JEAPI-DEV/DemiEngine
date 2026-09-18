---@meta
-- Native module: require("demi.tilemap2d"). Annotations only.
---@class Tilemap2DService
local Tilemap2D = {}
---@param entity_id string
---@param layer string
---@param column integer
---@param row integer
---@return integer|nil
function Tilemap2D.get_tile(entity_id, layer, column, row) end
---@param entity_id string
---@param layer string
---@param column integer
---@param row integer
---@param tile integer
---@return boolean
function Tilemap2D.set_tile(entity_id, layer, column, row, tile) end
---@param entity_id string
---@return boolean
function Tilemap2D.clear_overrides(entity_id) end
---@param entity_id string
---@return boolean
function Tilemap2D.bake_navigation(entity_id) end
---@class TilemapObject2D
---@field id string
---@field type string
---@field x number
---@field y number
---@field width number
---@field height number
---@field properties table
---@param entity_id string
---@param layer string
---@return TilemapObject2D[]
function Tilemap2D.objects(entity_id, layer) end

return Tilemap2D
