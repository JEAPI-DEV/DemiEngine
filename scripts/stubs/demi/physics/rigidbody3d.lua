---@meta
-- Native module: require("demi.physics.rigidbody3d"). Annotations only.
---@class Rigidbody3DService
-- Entity.create Rigidbody3D definitions accept solver_velocity_steps and
-- solver_position_steps (integers 0..128). Zero retains backend defaults;
-- higher values increase contact-solving work for the connected body island.
local Rigidbody3D = {}
---@param entity_id string
---@return number|nil x
---@return number|nil y
---@return number|nil z
function Rigidbody3D.get_velocity(entity_id) end
---@param entity_id string
---@param x number
---@param y number
---@param z number
---@return boolean
function Rigidbody3D.set_velocity(entity_id, x, y, z) end
---@param entity_id string
---@param x number
---@param y number
---@param z number
---@return boolean
function Rigidbody3D.add_force(entity_id, x, y, z) end
---@param entity_id string
---@param x number
---@param y number
---@param z number
---@return boolean
function Rigidbody3D.add_impulse(entity_id, x, y, z) end
---@param entity_id string
---@param x number
---@param y number
---@param z number
---@return boolean
function Rigidbody3D.add_torque(entity_id, x, y, z) end
---@param entity_id string
---@param awake boolean
---@return boolean
function Rigidbody3D.set_awake(entity_id, awake) end
---@param entity_id string
---@param enabled boolean
---@return boolean
function Rigidbody3D.set_enabled(entity_id, enabled) end
---@param entity_id string
---@param continuous boolean
---@return boolean
function Rigidbody3D.set_continuous(entity_id, continuous) end
---@param entity_id string
---@param report_contacts boolean
---@return boolean
function Rigidbody3D.set_report_contacts(entity_id, report_contacts) end
---@param entity_id string
---@param x number
---@param y number
---@param z number
---@param rotation_x number
---@param rotation_y number
---@param rotation_z number
---@param fixed_dt number
---@return boolean
function Rigidbody3D.move_kinematic(entity_id, x, y, z, rotation_x, rotation_y, rotation_z, fixed_dt) end

return Rigidbody3D
