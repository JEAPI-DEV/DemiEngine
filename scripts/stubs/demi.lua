---@meta

---@class DebugService
Debug = {}

---Writes a message to the DemiEngine runtime console.
---@param message string
function Debug.log(message) end

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

---@class EntityService
Entity = {}

---Finds an entity by stable id or display name. Returns the stable id when found.
---@param id_or_name string
---@return string|nil
function Entity.find(id_or_name) end

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

---@class DemiScript
---@field entity_id string
---@field speed number
local DemiScript = {}

function DemiScript:on_create() end
function DemiScript:on_start() end

---@param dt number
function DemiScript:on_update(dt) end

---@param dt number
function DemiScript:on_fixed_update(dt) end

function DemiScript:on_destroy() end

return DemiScript
