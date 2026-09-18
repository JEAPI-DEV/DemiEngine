---@meta
-- Native module: require("demi.math.vector3"). Annotations only.
---@class Vector3Service
local Vector3 = {}
---@param left number[]
---@param right number[]
---@return number[]
function Vector3.add(left, right) end
---@param left number[]
---@param right number[]
---@return number[]
function Vector3.subtract(left, right) end
---@param value number[]
---@param amount number
---@return number[]
function Vector3.scale(value, amount) end
---@param value number[]
---@return number
function Vector3.length(value) end
---@param value number[]
---@return number[]
function Vector3.normalized(value) end
---@param left number[]
---@param right number[]
---@return number
function Vector3.dot(left, right) end
---@param left number[]
---@param right number[]
---@return number[]
function Vector3.cross(left, right) end
---@param from number[]
---@param to number[]
---@param amount number
---@return number[]
function Vector3.lerp(from, to, amount) end

return Vector3
