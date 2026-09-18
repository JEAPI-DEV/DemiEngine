---@meta
-- Native module: require("demi.grid"). Annotations only.
---@class NetworkEvent
---@field type "connected"|"disconnected"|"message"
---@field peer_id integer
---@field channel integer
---@field message string
---@field latency_ms integer

---@class NetworkHttpResponse
---@field ok boolean
---@field status integer
---@field body string
---@field error string
---@field json table|nil

---@alias GridPoint {[1]: number, [2]: number}
---@alias GridPath GridPoint[]

---@class GridService
local Grid = {}
---@return boolean
function Grid.available() end
---@param x number
---@param y number
---@param elevation? number
---@return number|nil x
---@return number|nil y
function Grid.tile_to_world(x, y, elevation) end
---@param x number
---@param y number
---@return number|nil tile_x
---@return number|nil tile_y
function Grid.world_to_tile(x, y) end
---@param entity_id string
---@return number|nil tile_x
---@return number|nil tile_y
function Grid.get_tile(entity_id) end
---@param entity_id string
---@param x number
---@param y number
---@param elevation? number
---@return boolean
function Grid.set_tile(entity_id, x, y, elevation) end
---@param x integer
---@param y integer
---@return string|nil
function Grid.entity_at(x, y) end
---@param x integer
---@param y integer
---@param width integer
---@param height integer
---@return boolean allowed
---@return string diagnostic_code
---@return string diagnostic_message
function Grid.can_place(x, y, width, height) end
---@param start_x integer
---@param start_y integer
---@param goal_x integer
---@param goal_y integer
---@return GridPath|nil path
---@return string diagnostic_code
function Grid.path(start_x, start_y, goal_x, goal_y) end
---@param start_x integer
---@param start_y integer
---@param goal_x integer
---@param goal_y integer
---@param block_x integer
---@param block_y integer
---@param block_width integer
---@param block_height integer
---@return GridPath|nil path
---@return string diagnostic_code
function Grid.path_with_blocker(start_x, start_y, goal_x, goal_y, block_x,
                                block_y, block_width, block_height) end
---@param x integer
---@param y integer
---@param width integer
---@param height integer
---@param valid boolean
function Grid.set_preview(x, y, width, height, valid) end
function Grid.clear_preview() end

return Grid
