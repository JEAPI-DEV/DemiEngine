---@meta
-- Native module: require("demi.vector2"). Annotations only.
---@class Vector2Service
local Vector2 = {}
---@param left number[]
---@param right number[]
---@return number[]
function Vector2.add(left, right) end
---@param left number[]
---@param right number[]
---@return number[]
function Vector2.subtract(left, right) end
---@param value number[]
---@param amount number
---@return number[]
function Vector2.scale(value, amount) end
---@param value number[]
---@return number
function Vector2.length(value) end
---@param value number[]
---@return number[]
function Vector2.normalized(value) end
---@param left number[]
---@param right number[]
---@return number
function Vector2.dot(left, right) end
---@param from number[]
---@param to number[]
---@param amount number
---@return number[]
function Vector2.lerp(from, to, amount) end

return Vector2
