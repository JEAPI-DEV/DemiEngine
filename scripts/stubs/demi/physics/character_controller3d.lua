---@meta
-- Native module: require("demi.physics.character_controller3d"). Annotations only.
---@class CharacterController3DState
---@field velocity Vec3 Current world-space velocity in world units/second.
---@field grounded boolean
---@field ground_entity string
---@class CharacterController3DService
local CharacterController3D = {}
---@param entity_id string
---@param x number
---@param y number
---@param z number
---@return boolean
function CharacterController3D.set_velocity(entity_id, x, y, z) end
---@param entity_id string
---@param speed number Upward jump speed in world units/second.
---@return boolean
function CharacterController3D.jump(entity_id, speed) end
---@param entity_id string
---@return CharacterController3DState|nil
function CharacterController3D.state(entity_id) end

return CharacterController3D
