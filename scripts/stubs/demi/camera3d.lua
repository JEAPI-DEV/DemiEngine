---@meta
-- Native module: require("demi.camera3d"). Annotations only.
---@class CameraRay3D
---@field origin number[]
---@field direction number[]
---@class Camera3DService
---Screen conversions follow the rendered target_offset and up_axis.
---Orthographic size is the full visible height; coordinates are viewport-local.
local Camera3D = {}
---@param entity_id string
---@param screen_x number
---@param screen_y number
---@param viewport_width number
---@param viewport_height number
---@return CameraRay3D|nil
function Camera3D.screen_ray(entity_id, screen_x, screen_y, viewport_width, viewport_height) end
---@param entity_id string
---@param world_x number
---@param world_y number
---@param world_z number
---@param viewport_width number
---@param viewport_height number
---@return number[]|nil
function Camera3D.world_to_screen(entity_id, world_x, world_y, world_z, viewport_width, viewport_height) end
---@param entity_id string
---@param screen_x number
---@param screen_y number
---@param viewport_width number
---@param viewport_height number
---@param distance number
---@return number|nil x
---@return number|nil y
---@return number|nil z
function Camera3D.screen_to_world(entity_id, screen_x, screen_y, viewport_width, viewport_height, distance) end

return Camera3D
