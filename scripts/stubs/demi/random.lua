---@meta
-- Native module: require("demi.random"). Annotations only.
---@class RandomService
local Random = {}
---@param seed integer
function Random.seed(seed) end
---@return string Exact unsigned 64-bit state suitable for JSON saves.
function Random.state() end
---@param state string State previously returned by Random.state.
---@return boolean
function Random.restore(state) end
---@return number
function Random.value() end
---@param minimum number
---@param maximum number
---@return number
function Random.range(minimum, maximum) end
---@param minimum integer
---@param maximum integer
---@return integer
function Random.integer(minimum, maximum) end

return Random
