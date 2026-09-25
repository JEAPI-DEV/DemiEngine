---@meta
-- Native module: require("demi.physics.rigidbody2d"). Annotations only.
---@class Rigidbody2DService
local Rigidbody2D = {}
---@param entity_id string
---@return number|nil x
---@return number|nil y
function Rigidbody2D.get_velocity(entity_id) end
---@param entity_id string
---@param x number
---@param y number
---@return boolean
function Rigidbody2D.set_velocity(entity_id, x, y) end
---@param entity_id string
---@param x number
---@return boolean
function Rigidbody2D.set_velocity_x(entity_id, x) end
---@param entity_id string
---@param y number
---@return boolean
function Rigidbody2D.set_velocity_y(entity_id, y) end
---@param entity_id string
---@param x number
---@param y number
---@return boolean
function Rigidbody2D.add_impulse(entity_id, x, y) end
---@param entity_id string
---@param x number
---@param y number
---@return boolean
function Rigidbody2D.add_force(entity_id, x, y) end
---@param entity_id string
---@param torque number
---@return boolean
function Rigidbody2D.add_torque(entity_id, torque) end
---@param entity_id string
---@param angular_velocity number Radians per second.
---@return boolean
function Rigidbody2D.set_angular_velocity(entity_id, angular_velocity) end
---@param entity_id string
---@param awake boolean
---@return boolean
function Rigidbody2D.set_awake(entity_id, awake) end
---@param entity_id string
---@param enabled boolean
---@return boolean
function Rigidbody2D.set_enabled(entity_id, enabled) end
---@param entity_id string
---@param continuous boolean
---@return boolean
function Rigidbody2D.set_continuous(entity_id, continuous) end
---@param entity_id string
---@param report_contacts boolean
---@return boolean
function Rigidbody2D.set_report_contacts(entity_id, report_contacts) end
---@param entity_id string
---@param x number
---@param y number
---@param fixed_dt? number Seconds; defaults to 1/60. Pass the actual fixed timestep explicitly.
---@return boolean
function Rigidbody2D.move_kinematic(entity_id, x, y, fixed_dt) end
---@param entity_id string
---@param motion_x number
---@param motion_y number
---@return number?, number?
function Rigidbody2D.move_and_slide(entity_id, motion_x, motion_y) end

return Rigidbody2D
