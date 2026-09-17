---@meta
-- Native module: require("demi.physics"). Annotations only.
---@class PhysicsService
local Physics = {}
---@param enabled boolean
function Physics.set_enabled(enabled) end
---@return boolean
function Physics.enabled() end
---@param entity_id string entity to match on either side of the contact
---@param callback fun(contact: table) receives enter-phase trigger payloads (2D + 3D)
---@return integer sub_2d
---@return integer sub_3d
function Physics.on_trigger(entity_id, callback) end
---@param entity_id string entity to match on either side of the contact
---@param callback fun(contact: table) receives enter-phase collision payloads (2D + 3D)
---@return integer sub_2d
---@return integer sub_3d
function Physics.on_collision(entity_id, callback) end

return Physics
