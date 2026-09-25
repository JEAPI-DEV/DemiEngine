---@meta
-- Native module: require("demi.physics.query2d"). Annotations only.
---@class Physics2DService
---Queries use world coordinates. Bulk queries return an empty array on no hits;
---raycast returns nil. Contact normals are scalar fields, unlike hit.normal.
local Physics2D = {}
---@param x number
---@param y number
---@param width number
---@param height number
---@param ignored_entity_id? string
---@return boolean
function Physics2D.overlap_box(x, y, width, height, ignored_entity_id) end
---@class PhysicsRaycastHit2D
---@field entity_id string
---@field layer string
---@field point Vec2 World-space hit point.
---@field normal Vec2 World-space hit normal.
---@field distance number
---@field fraction number
---@param x number
---@param y number
---@param radius number
---@param layer? string
---@param ignored_entity_id? string
---@return string[]
function Physics2D.overlap_circle(x, y, radius, layer, ignored_entity_id) end
---@param x number
---@param y number
---@param width number
---@param height number
---@param layer? string
---@param ignored_entity_id? string
---@return PhysicsRaycastHit2D[]
function Physics2D.overlap_box_all(x, y, width, height, layer, ignored_entity_id) end
---@param x number
---@param y number
---@param radius number
---@param layer? string
---@param ignored_entity_id? string
---@return PhysicsRaycastHit2D[]
function Physics2D.overlap_circle_all(x, y, radius, layer, ignored_entity_id) end
---@param origin_x number
---@param origin_y number
---@param direction_x number
---@param direction_y number
---@param distance number
---@param layer? string
---@param ignored_entity_id? string
---@return PhysicsRaycastHit2D|nil
function Physics2D.raycast(origin_x, origin_y, direction_x, direction_y, distance, layer, ignored_entity_id) end

---@class PhysicsContact2D
---@field entity_id string
---@field other_entity_id string
---@field other_layer string
---@field phase "enter"|"stay"|"exit"
---@field point Vec2
---@field normal_x number
---@field normal_y number
---@field normal_impulse number
---@field is_trigger boolean
---@param entity_id string
---@return PhysicsContact2D[]
function Physics2D.contacts(entity_id) end


---@class PhysicsContactFilter2D
---@field layer? string
---@field normal_x_min? number
---@field normal_x_max? number
---@field normal_y_min? number
---@field normal_y_max? number
---@field include_triggers? boolean
---@param entity_id string
---@param filter? PhysicsContactFilter2D
---@return boolean
function Physics2D.has_contact(entity_id, filter) end

return Physics2D
