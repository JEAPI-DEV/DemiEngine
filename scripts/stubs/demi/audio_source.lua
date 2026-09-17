---@meta
-- Native module: require("demi.audio_source"). Annotations only.
---@class AudioSourceService
local AudioSource = {}
---@param entity_id string
---@return integer handle
function AudioSource.play(entity_id) end
---@param entity_id string
---@return boolean
function AudioSource.stop(entity_id) end

return AudioSource
