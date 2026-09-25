---@meta
-- Native module: require("demi.camera3d"). Annotations only.
---@class CameraRay3D
---@field origin Vec3 World-space ray origin.
---@field direction Vec3 World-space unit direction.
---@class Camera3DService
---Screen conversions follow the rendered target_offset and up_axis.
---Orthographic size is the full visible height; coordinates are viewport-local.
---Screen coordinates use a top-left origin, +X right, +Y down, in the same units
---as viewport dimensions. Width and height are clamped to at least 1.
local Camera3D = {}
---@param entity_id string
---@param screen_x number
---@param screen_y number
---@param viewport_width number
---@param viewport_height number
---@return CameraRay3D|nil ray Nil without a camera or resolvable transform.
function Camera3D.screen_ray(entity_id, screen_x, screen_y, viewport_width, viewport_height) end
---@param entity_id string
---@param world_x number
---@param world_y number
---@param world_z number
---@param viewport_width number
---@param viewport_height number
---@return Vec2|nil screen Nil without a camera/transform or outside near/far depth; offscreen X/Y are not clipped.
function Camera3D.world_to_screen(entity_id, world_x, world_y, world_z, viewport_width, viewport_height) end
---@param entity_id string
---@param screen_x number
---@param screen_y number
---@param viewport_width number
---@param viewport_height number
---@param distance number World distance along the normalized screen ray, clamped to >= 0 (not camera-forward depth).
---@return number|nil x World-space X; all three results are nil without a camera/transform.
---@return number|nil y World-space Y.
---@return number|nil z World-space Z.
function Camera3D.screen_to_world(entity_id, screen_x, screen_y, viewport_width, viewport_height, distance) end

return Camera3D
