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

---@class EntityService
Entity = {}

---Moves an entity with a Transform2D component by a world-space delta.
---@param entity_id string
---@param dx number
---@param dy number
function Entity.add_position(entity_id, dx, dy) end

---Returns the current world-space position for an entity with Transform2D.
---@param entity_id string
---@return number|nil x
---@return number|nil y
function Entity.get_position(entity_id) end

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
