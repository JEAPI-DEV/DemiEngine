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

---@return number width
---@return number height
function Input.viewport_size() end

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

---Destroys an entity by stable id.
---@param entity_id string
---@return boolean
function Entity.destroy(entity_id) end

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

---@class RuntimeService
Runtime = {}

function Runtime.quit() end

---@param enabled boolean
function Runtime.set_physics_enabled(enabled) end

---@param mode string "windowed", "borderless", or "fullscreen"
function Runtime.set_window_mode(mode) end

---@return string mode
function Runtime.get_window_mode() end

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

---@class HudService
Hud = {}

---@param id string
---@param text string
---@param x number
---@param y number
---@param scale? number
---@param r? number
---@param g? number
---@param b? number
---@param a? number
---@return boolean
function Hud.text(id, text, x, y, scale, r, g, b, a) end

---@param id string
---@param x number
---@param y number
---@param width number
---@param height number
---@param r? number
---@param g? number
---@param b? number
---@param a? number
---@return boolean
function Hud.rect(id, x, y, width, height, r, g, b, a) end

---@param id string
---@param text string
---@return boolean
function Hud.set_text(id, text) end

---@param id string
---@param x number
---@param y number
---@param width number
---@param height number
---@return boolean
function Hud.set_rect(id, x, y, width, height) end

---@param id string
---@param r number
---@param g number
---@param b number
---@param a? number
---@return boolean
function Hud.set_color(id, r, g, b, a) end

---@param id string
---@param visible boolean
---@return boolean
function Hud.set_visible(id, visible) end

---@param group string
---@param visible boolean
---@return boolean
function Hud.set_group_visible(group, visible) end

---@param id string
---@return string|nil
function Hud.get_text(id) end

---@class AudioService
Audio = {}

---Plays an audio asset by asset:// id.
---@param asset_id string
---@return integer handle
function Audio.play(asset_id) end

---Stops a playing audio handle returned by Audio.play.
---@param handle integer
---@return boolean
function Audio.stop(handle) end

---@param volume number 0.0 to 1.0
function Audio.set_master_volume(volume) end

---@return number volume
function Audio.get_master_volume() end

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

---@class UiEvent
---@field id string
---@field label string
---@field mouse_x number
---@field mouse_y number

---@param event UiEvent
function DemiScript:on_ui_hover(event) end

---@param event UiEvent
function DemiScript:on_ui_click(event) end

return DemiScript
