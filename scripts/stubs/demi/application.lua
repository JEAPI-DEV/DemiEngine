---@meta
-- Native module: require("demi.application"). Annotations only.
---@class ApplicationService
local Application = {}
function Application.quit() end
---@return "android"|"windows"|"macos"|"linux"|"unknown"
function Application.platform() end
---@param mode string
function Application.set_window_mode(mode) end
---@return string mode
function Application.window_mode() end
---@param max_fps number
function Application.set_max_fps(max_fps) end
---@return integer max_fps
function Application.max_fps() end
---@param captured boolean
function Application.set_mouse_captured(captured) end
---@return boolean captured
function Application.mouse_captured() end
---@return number left
---@return number top
---@return number right
---@return number bottom
function Application.safe_area() end
---@return number
function Application.logical_dpi() end
---@return number
function Application.ui_scale() end
---@return "portrait"|"landscape"|"unspecified"
function Application.orientation() end
---@param orientation "portrait"|"landscape"|"unspecified"
---@return boolean
function Application.request_orientation(orientation) end
---@return boolean
function Application.keyboard_visible() end
---@return string
function Application.clipboard() end
---@param text string
function Application.set_clipboard(text) end
---@return boolean
function Application.focused() end
---@return boolean
function Application.minimized() end
---@return boolean
function Application.suspended() end
---@return integer
function Application.low_memory_generation() end
---@return string
function Application.user_data_path() end
---@return string
function Application.cache_path() end
---@alias PermissionState "unknown"|"not_requested"|"requesting"|"granted"|"denied"|"denied_permanently"
---@param permission string
---@return PermissionState
function Application.permission_state(permission) end
---@param permission string Must be declared in project build.android.permissions.
---@return boolean requested
---@return string error
function Application.request_permission(permission) end
---@return {permission: string, state: PermissionState}[]
function Application.take_permission_events() end
---@alias ApplicationLifecycleEventType "focus_gained"|"focus_lost"|"minimized"|"restored"|"suspended"|"resumed"|"low_memory"|"display_changed"|"safe_area_changed"|"back_requested"
---@return {type: ApplicationLifecycleEventType, generation: integer}[]
function Application.take_lifecycle_events() end

return Application
