---@meta
-- Native module: require("demi.math.vector3"). Annotations only.
---@class Vector3Service
local Vector3 = {}
---@param left Vec3
---@param right Vec3
---@return Vec3
function Vector3.add(left, right) end
---@param left Vec3
---@param right Vec3
---@return Vec3
function Vector3.subtract(left, right) end
---@param value Vec3
---@param amount number
---@return Vec3
function Vector3.scale(value, amount) end
---@param value Vec3
---@return number
function Vector3.length(value) end
---@param value Vec3
---@return Vec3
function Vector3.normalized(value) end
---@param left Vec3
---@param right Vec3
---@return number
function Vector3.dot(left, right) end
---@param left Vec3
---@param right Vec3
---@return Vec3
function Vector3.cross(left, right) end
---@param from Vec3
---@param to Vec3
---@param amount number
---@return Vec3
function Vector3.lerp(from, to, amount) end

return Vector3
