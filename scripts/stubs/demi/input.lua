---@meta
-- Native module: require("demi.input"). Annotations only.
---@class InputService
local Input = {}
---@param key string
---@return boolean
function Input.is_down(key) end
---@param key string
---@return boolean
function Input.is_pressed(key) end
---@param key string
---@return boolean
function Input.is_released(key) end
---@param action string
---@param player? integer
---@return boolean
function Input.action_down(action, player) end
---@param action string
---@param player? integer
---@return boolean
function Input.action_pressed(action, player) end
---@param action string
---@param player? integer
---@return boolean
function Input.action_released(action, player) end
---@param action string
---@param player? integer
---@return number
function Input.action_value(action, player) end
---@param action string
---@param player? integer
---@return number x
---@return number y
function Input.action_vector(action, player) end
---@param action string Vector2 action; result normalized to length <= 1 (diagonal-safe).
---@param player? integer
---@return number x
---@return number y
function Input.vector(action, player) end
---@param action string
---@param player? integer
---@return boolean
function Input.pressed(action, player) end
---@param action string
---@param player? integer
---@return boolean
function Input.down(action, player) end
---@param action string
---@param player? integer
---@return number
function Input.value(action, player) end
---@param action string
---@param player? integer
---@return string
function Input.action_source(action, player) end
---@param context string
function Input.enable_context(context) end
---@param context string
function Input.disable_context(context) end
---@param context string
---@return boolean
function Input.context_enabled(context) end
---@param action string
---@param binding integer One-based binding index.
---@param control string
---@param player? integer
---@return boolean success
---@return string error
function Input.rebind(action, binding, control, player) end
---@param path string
---@return boolean success
---@return string error
function Input.save_bindings(path) end
---@param path string
---@return boolean success
---@return string error
function Input.load_bindings(path) end
---@param device integer
---@param player integer
---@return boolean
function Input.assign_gamepad(device, player) end
---@return integer
function Input.gamepad_count() end
---@return integer
function Input.touch_count() end
---@class TouchPoint
---@field id integer
---@field phase "began"|"moved"|"stationary"|"ended"|"cancelled"
---@field x number
---@field y number
---@field dx number
---@field dy number
---@field pressure number
---@return TouchPoint[]
function Input.touches() end
---@class GestureEvent
---@field type "tap"|"double_tap"|"long_press"|"drag"|"pinch"|"rotate"
---@field pointer_id integer
---@field x number
---@field y number
---@field dx number
---@field dy number
---@field value number
---@return GestureEvent[]
function Input.gestures() end
---@return string
function Input.text_entered() end
---@param active boolean
function Input.set_text_input_active(active) end
---@param button string
---@return boolean
function Input.mouse_down(button) end
---@return number x
---@return number y
function Input.mouse_position() end
---@return number dx
---@return number dy
function Input.mouse_delta() end
---@return number x
---@return number y
function Input.mouse_world_position() end
---@return number width
---@return number height
function Input.viewport_size() end
---@param pointer_id? integer
---@return boolean
function Input.ui_pointer_captured(pointer_id) end

return Input
