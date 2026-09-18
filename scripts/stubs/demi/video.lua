---@meta
-- Native module: require("demi.video"). Annotations only.
---@class VideoService
local Video = {}
---@param asset_id string
---@param loop? boolean
---@return integer handle
function Video.play(asset_id, loop) end
---@param entity_id string
---@return integer handle
function Video.play_component(entity_id) end
---@param handle integer
---@return boolean
function Video.stop(handle) end
---@param handle integer
---@return boolean
function Video.is_playing(handle) end

return Video
