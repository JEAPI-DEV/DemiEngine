---@meta
-- Native module: require("demi.math.vector2"). Annotations only.
---@class Vector2Service
local Vector2 = {}
---@param left Vec2
---@param right Vec2
---@return Vec2
function Vector2.add(left, right) end
---@param left Vec2
---@param right Vec2
---@return Vec2
function Vector2.subtract(left, right) end
---@param value Vec2
---@param amount number
---@return Vec2
function Vector2.scale(value, amount) end
---@param value Vec2
---@return number
function Vector2.length(value) end
---@param value Vec2
---@return Vec2
function Vector2.normalized(value) end
---@param left Vec2
---@param right Vec2
---@return number
function Vector2.dot(left, right) end
---@param from Vec2
---@param to Vec2
---@param amount number
---@return Vec2
function Vector2.lerp(from, to, amount) end

return Vector2
