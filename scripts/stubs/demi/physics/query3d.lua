---@meta
-- Native module: require("demi.physics.query3d"). Annotations only.
---@class Physics3DService
---Queries use world coordinates. Bulk queries return an empty array on no hits;
---raycast and sphere_cast return nil. Distances and extents use world units.
local Physics3D = {}
---@class PhysicsRaycastHit3D
---@field entity_id string
---@field collider_part_id? string Stable compound part ID for raycasts; absent for other shapes/queries.
---@field layer string
---@field point Vec3 World-space hit point.
---@field normal Vec3 World-space hit normal.
---@field distance number
---@field fraction number
---@field is_trigger boolean
---@param x number
---@param y number
---@param z number
---@param radius number
---@param ignored_entity_id? string
---@return string[]
function Physics3D.overlap_sphere(x, y, z, radius, ignored_entity_id) end
---@param x number
---@param y number
---@param z number
---@param radius number
---@param layer? string
---@param ignored_entity_id? string
---@return PhysicsRaycastHit3D[]
function Physics3D.overlap_sphere_all(x, y, z, radius, layer, ignored_entity_id) end
---@param x number
---@param y number
---@param z number
---@param width number
---@param height number
---@param depth number
---@param layer? string
---@param ignored_entity_id? string
---@return PhysicsRaycastHit3D[]
function Physics3D.overlap_box_all(x, y, z, width, height, depth, layer, ignored_entity_id) end
---The optional final argument is an ignored entity ID, not a layer filter.
---@param origin_x number
---@param origin_y number
---@param origin_z number
---@param direction_x number
---@param direction_y number
---@param direction_z number
---@param distance number
---@param ignored_entity_id? string
---@return PhysicsRaycastHit3D|nil
function Physics3D.raycast(origin_x, origin_y, origin_z, direction_x, direction_y, direction_z, distance, ignored_entity_id) end
---@param origin_x number
---@param origin_y number
---@param origin_z number
---@param radius number
---@param direction_x number
---@param direction_y number
---@param direction_z number
---@param distance number
---@param layer? string
---@param ignored_entity_id? string
---@param include_triggers? boolean Defaults to true; false finds the first solid collider beyond triggers.
---@return PhysicsRaycastHit3D|nil
function Physics3D.sphere_cast(origin_x, origin_y, origin_z, radius, direction_x, direction_y, direction_z, distance, layer, ignored_entity_id, include_triggers) end

return Physics3D
