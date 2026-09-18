---@meta
-- Native module: require("demi.profile"). Annotations only.
---@class ProfileService
local Profile = {}
---@return boolean
function Profile.enabled() end
---@param name string
---@param callback fun()
---@return boolean
function Profile.scope(name, callback) end

return Profile
