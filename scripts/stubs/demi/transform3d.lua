---@meta
-- Native module: require("demi.transform3d"). Annotations only.
---@class Transform3DService
local Transform3D = {}
---@param entity_id string
---@return number|nil x
---@return number|nil y
---@return number|nil z
function Transform3D.get_position(entity_id) end
---@param entity_id string
---@param x number
---@param y number
---@param z number
---@return boolean
function Transform3D.set_position(entity_id, x, y, z) end
---@param entity_id string
---@param dx number
---@param dy number
---@param dz number
---@return boolean
function Transform3D.add_position(entity_id, dx, dy, dz) end
---@param entity_id string
---@return number|nil x
---@return number|nil y
---@return number|nil z
function Transform3D.get_rotation(entity_id) end
---@param entity_id string
---@param x number
---@param y number
---@param z number
---@return boolean
function Transform3D.set_rotation(entity_id, x, y, z) end
---@param entity_id string
---@return number|nil x
---@return number|nil y
---@return number|nil z
function Transform3D.get_scale(entity_id) end
---@param entity_id string
---@param x number
---@param y number
---@param z number
---@return boolean
function Transform3D.set_scale(entity_id, x, y, z) end
---@param entity_id string
---@return number|nil x
---@return number|nil y
---@return number|nil z
function Transform3D.forward(entity_id) end
---@param entity_id string
---@return number|nil x
---@return number|nil y
---@return number|nil z
function Transform3D.right(entity_id) end
---@param entity_id string
---@return number|nil x
---@return number|nil y
---@return number|nil z
function Transform3D.up(entity_id) end
---@param entity_id string
---@param x number
---@param y number
---@param z number
---@return boolean
function Transform3D.look_at(entity_id, x, y, z) end

return Transform3D
