---@meta

-- Project scripts can use standard Lua `require(...)` for modules inside the
-- project root or project `scripts/` directory, for example:
-- local movement = require("player_platformer")

---@class DebugService
Debug = {}

---Writes a message to the DemiEngine runtime console.
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
function Debug.line(x1, y1, x2, y2, r, g, b, a) end

function Debug.clear_lines() end

---@class InputService
Input = {}

---Returns true while a key is pressed. Key names are lowercase, such as "w", "a", "left", or "space".
---@param key string
---@return boolean
function Input.is_down(key) end

---@param negative_key string
---@param positive_key string
---@return number
function Input.axis(negative_key, positive_key) end

---@param left string
---@param right string
---@param down string
---@param up string
---@return number x
---@return number y
function Input.vector(left, right, down, up) end

---@param button string "left", "right", or "middle"
---@return boolean
function Input.mouse_down(button) end

---@return number x
---@return number y
function Input.mouse_position() end

---@return number x
---@return number y
function Input.mouse_world_position() end

---@class EntityService
Entity = {}

---Finds an entity by stable id or display name. Returns the stable id when found.
---@param id_or_name string
---@return string|nil
function Entity.find(id_or_name) end

---Creates or replaces an entity from a component table.
---@param entity_id string
---@param spec table
---@return boolean
function Entity.create(entity_id, spec) end

---Moves an entity with a Transform2D component by a world-space delta.
---@param entity_id string
---@param dx number
---@param dy number
function Entity.add_position(entity_id, dx, dy) end

---Sets an entity position when it has Transform2D.
---@param entity_id string
---@param x number
---@param y number
---@return boolean
function Entity.set_position(entity_id, x, y) end

---Returns the current world-space position for an entity with Transform2D.
---@param entity_id string
---@return number|nil x
---@return number|nil y
function Entity.get_position(entity_id) end

---@class TimeService
---@field delta_time number
Time = {}

---@class SceneService
Scene = {}

---Planned scene loading API. Currently returns false.
---@param scene_id string
---@return boolean
function Scene.load(scene_id) end

---@class Rigidbody2DService
Rigidbody2D = {}

---@param entity_id string
---@return number|nil x
---@return number|nil y
function Rigidbody2D.get_velocity(entity_id) end

---@param entity_id string
---@param x number
---@param y number
---@return boolean
function Rigidbody2D.set_velocity(entity_id, x, y) end

---@param entity_id string
---@param x number
---@return boolean
function Rigidbody2D.set_velocity_x(entity_id, x) end

---@param entity_id string
---@param y number
---@return boolean
function Rigidbody2D.set_velocity_y(entity_id, y) end

---@param entity_id string
---@param x number
---@param y number
---@return boolean
function Rigidbody2D.add_impulse(entity_id, x, y) end

---@class Physics2DService
Physics2D = {}

---@param x number
---@param y number
---@param width number
---@param height number
---@param ignored_entity_id? string
---@return boolean
function Physics2D.overlap_box(x, y, width, height, ignored_entity_id) end

---@class DemiScript
---@field entity_id string
---@field speed number
---@field jump_speed number
local DemiScript = {}

function DemiScript:on_create() end
function DemiScript:on_start() end

---@param dt number
function DemiScript:on_update(dt) end

---@param dt number
function DemiScript:on_fixed_update(dt) end

function DemiScript:on_destroy() end

return DemiScript
