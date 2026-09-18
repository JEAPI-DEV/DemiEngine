---@meta
-- Native module: require("demi.navigation2d"). Annotations only.
---@class Navigation2DService
local Navigation2D = {}
---@param width integer
---@param height integer
---@param cell_size number
---@param origin_x? number
---@param origin_y? number
---@return boolean
function Navigation2D.configure(width, height, cell_size, origin_x, origin_y) end
function Navigation2D.clear() end
---@return boolean
function Navigation2D.available() end
---@param x integer
---@param y integer
---@param blocked boolean
---@return boolean
function Navigation2D.set_blocked(x, y, blocked) end
---@param x integer
---@param y integer
---@param cost number
---@return boolean
function Navigation2D.set_cost(x, y, cost) end
---@param start_x integer
---@param start_y integer
---@param goal_x integer
---@param goal_y integer
---@param diagonal? boolean
---@return table path
---@return string diagnostic
function Navigation2D.path(start_x, start_y, goal_x, goal_y, diagonal) end
---@param x number
---@param y number
---@return integer|nil column
---@return integer|nil row
function Navigation2D.world_to_cell(x, y) end
---@param column integer
---@param row integer
---@return number|nil x
---@return number|nil y
function Navigation2D.cell_to_world(column, row) end

return Navigation2D
