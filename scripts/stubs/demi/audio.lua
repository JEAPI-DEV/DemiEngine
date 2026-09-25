---@meta
-- Native module: require("demi.audio"). Annotations only.
---@class AudioService
local Audio = {}
---@class AudioPlayOptions
---@field bus? "music"|"sfx"|"voice"|"ui"|string
---@field loop? boolean
---@field streaming? boolean
---@field volume? number
---@field pitch? number
---@field pan? number
---@field spatial? "none"|"2d"|"3d"
---@field attenuation? "none"|"inverse"|"linear"|"exponential"
---@field x? number
---@field y? number
---@field z? number
---@field min_distance? number
---@field max_distance? number
---@field rolloff? number
---@field doppler? boolean
---@field delay? number Seconds.
---@field fade_in? number Seconds.
---@field concurrency_group? string
---@field max_voices? integer
---@field voice_stealing? "reject"|"oldest"|"quietest"
---@field pause_with_game? boolean
---@param asset_id string
---@param options? AudioPlayOptions
---@return integer handle
function Audio.play(asset_id, options) end
---@param handle integer
---@return boolean
function Audio.stop(handle) end
---@param volume number
function Audio.set_master_volume(volume) end
---@return number
function Audio.get_master_volume() end
---@param bus string
---@param volume number
---@return boolean
function Audio.set_bus_volume(bus, volume) end
---@param bus string
---@return number
function Audio.get_bus_volume(bus) end
---@param bus string
---@param muted boolean
---@return boolean
function Audio.set_bus_muted(bus, muted) end
---@param bus string
---@param paused boolean
---@return boolean
function Audio.set_bus_paused(bus, paused) end
---@param name string
---@param volumes table<string, number>
function Audio.define_snapshot(name, volumes) end
---@param name string
---@param duration number
---@return boolean
function Audio.transition_snapshot(name, duration) end
---@param from_handle integer
---@param asset_id string
---@param duration number
---@param bus? string
---@param loop? boolean
---@param streaming? boolean
---@return integer handle
function Audio.crossfade(from_handle, asset_id, duration, bus, loop, streaming) end

return Audio
