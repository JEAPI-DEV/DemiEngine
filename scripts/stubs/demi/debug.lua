---@meta
-- Native module: require("demi.debug"). Annotations only.
---@class DebugService
local Debug = {}
---@param message string
function Debug.log(message) end
---@param x1 number
---@param y1 number
---@param x2 number
---@param y2 number
---@param r? number
---@param g? number
---@param b? number
---@param a? number
---@param width? number
function Debug.line(x1, y1, x2, y2, r, g, b, a, width) end
function Debug.clear_lines() end

return Debug
