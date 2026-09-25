---@meta
-- Native module: require("demi.transform2d"). Annotations only.
---@class TransformService
---Position, rotation and scale access the local Transform2D component.
---Rotation is in radians. Vector getters return x,y or nil,nil when unavailable;
---mutators return false when the entity/component is missing.
local Transform = {}
---@param entity_id string
---@return number|nil x
---@return number|nil y
function Transform.get_position(entity_id) end
---@param entity_id string
---@param x number
---@param y number
---@return boolean
function Transform.set_position(entity_id, x, y) end
---@param entity_id string
---@param dx number
---@param dy number
---@return boolean
function Transform.add_position(entity_id, dx, dy) end
---@param entity_id string
---@return number|nil rotation Local angle in radians.
function Transform.get_rotation(entity_id) end
---@param entity_id string
---@param rotation number Local angle in radians.
---@return boolean
function Transform.set_rotation(entity_id, rotation) end
---@param entity_id string
---@return number|nil x
---@return number|nil y
function Transform.get_scale(entity_id) end
---@param entity_id string
---@param x number
---@param y number
---@return boolean
function Transform.set_scale(entity_id, x, y) end

return Transform
