---@meta
-- Native module: require("demi.time"). Annotations only.
---@class TimeService
---@field delta_time number
---@field unscaled_delta_time number
---@field time number
---@field fixed_time number
---@field frame_count integer
---@field time_scale number
---@field paused boolean
local Time = {}
---@param paused boolean
function Time.set_paused(paused) end
---@return boolean
function Time.is_paused() end
---@param scale number
function Time.set_scale(scale) end
---@return number
function Time.get_scale() end

return Time
