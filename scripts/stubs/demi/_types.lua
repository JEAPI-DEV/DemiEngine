---@meta
---@class DemiScript
---@field entity_id? string
---@field ui_id? string
---@field speed? number
---@field jump_speed? number
local DemiScript = {}
function DemiScript:on_create() end
function DemiScript:on_start() end
---@param dt number
function DemiScript:on_update(dt) end
---@param dt number
function DemiScript:on_fixed_update(dt) end
function DemiScript:on_destroy() end

---@alias DemiUiEventType "value_changed"|"focus_gained"|"focus_lost"|"submit"|"cancel"|"pointer_enter"|"pointer_exit"|"press"|"release"|"drag_start"|"drag"|"drag_end"|"drop"|"scroll"
---@alias DemiUiEventSource "mouse"|"touch"|"keyboard"|"controller"|"state_change"|"node_removed"
---@class DemiTypedUiEvent
---@field type DemiUiEventType
---@field id string
---@field related_id string Source for a drop, or the other node for pointer enter/exit.
---@field action string
---@field text string
---@field source DemiUiEventSource|string
---@field pointer_id integer -1 when the event has no pointer.
---@field x number Position in HUD canvas coordinates.
---@field y number Position in HUD canvas coordinates.
---@field delta_x number
---@field delta_y number
---@field value number
---@field checked boolean
---@field cancelled boolean
---@param event DemiTypedUiEvent
function DemiScript:on_ui_event(event) end
---@param event DemiTypedUiEvent
function DemiScript:on_ui_value_changed(event) end
---@param event DemiTypedUiEvent
function DemiScript:on_ui_focus_gained(event) end
---@param event DemiTypedUiEvent
function DemiScript:on_ui_focus_lost(event) end
---@param event DemiTypedUiEvent
function DemiScript:on_ui_submit(event) end
---@param event DemiTypedUiEvent
function DemiScript:on_ui_cancel(event) end
---@param event DemiTypedUiEvent
function DemiScript:on_ui_pointer_enter(event) end
---@param event DemiTypedUiEvent
function DemiScript:on_ui_pointer_exit(event) end
---@param event DemiTypedUiEvent
function DemiScript:on_ui_press(event) end
---@param event DemiTypedUiEvent
function DemiScript:on_ui_release(event) end
---@param event DemiTypedUiEvent
function DemiScript:on_ui_drag_start(event) end
---@param event DemiTypedUiEvent
function DemiScript:on_ui_drag(event) end
---@param event DemiTypedUiEvent
function DemiScript:on_ui_drag_end(event) end
---@param event DemiTypedUiEvent
function DemiScript:on_ui_drop(event) end
---@param event DemiTypedUiEvent
function DemiScript:on_ui_scroll(event) end

-- Generated from ComponentRegistry metadata.
---@class DemiRigidbody2DSpec
---@field body_type? string
---@field velocity? number[]
---@field gravity_scale? number
---@field bounciness? number
---@field lock_rotation? boolean
---@field angular_velocity? number
---@field linear_damping? number
---@field angular_damping? number
---@field continuous? boolean
---@field allow_sleep? boolean
---@field awake? boolean
---@field body_enabled? boolean

---@class DemiSpriteAnimator2DSpec
---@field frame_size? number[]
---@field atlas? table
---@field clips? table
---@field clip? string
---@field speed? number
---@field playing? boolean

---@class DemiTransform2DSpec
---@field parent? string
---@field position? number[]
---@field rotation? number
---@field scale? number[]

---@class DemiTransform3DSpec
---@field parent? string
---@field position? number[]
---@field rotation? number[]
---@field scale? number[]

---@class DemiAnimationStateMachineSpec
---@field states table
---@field transitions? table
---@field parameters? table
---@field blend_spaces? table
---@field layers? table
---@field initial_state? string
---@field speed? number
---@field root_motion? boolean
---@field pause_policy? string

---@class DemiScriptModule
---@field bind fun(self: table): table Wrap a script table; enables self:on/self:after/self:move helpers.
---@field release fun(self: table) Release subscriptions/timers created via self:on/self:after. Call from on_destroy.
---@field on fun(self: table, event_name: string, callback: fun(payload: table)): integer
---@field after fun(self: table, seconds: number, callback: fun(timer_id: integer)): integer
---@field move fun(self: table, dx: number, dy: number): boolean
---@field teleport fun(self: table, x: number, y: number): boolean
---@field move3d fun(self: table, dx: number, dy: number, dz: number): boolean
---@field set_text fun(self: table, node_id: string, text: string): boolean
---@field input_vector fun(self: table, action: string, player?: integer): number, number

---@class DemiUiModule
---@field bind_list fun(collection_id: string, template_id: string, keys: string[], extents: number[], scroll_offset: number, viewport_extent: number, overscan?: integer, render_fn?: fun(row: table, key: string)): table
---@field filter_list fun(collection_id: string, template_id: string, items: table[], pattern: string, key_fn: fun(item: table): string, match_fn: fun(item: table, pattern: string): boolean, render_fn: fun(row: table, item: table), row_extent?: number, viewport_extent?: number, overscan?: integer): table
---@field scroll_panel fun(panel_id: string, row_extent: number, viewport_extent: number, item_count_fn: fun(): integer, apply_fn?: fun(scroll: number)): table
---@field tabs fun(tabs: table<string,string>, focus?: table<string,string>)
---@field show_only fun(visible_id: string, hidden_ids?: string[])
---@field dropdown fun(button_id: string, options_id: string, open: boolean, label?: string)
---@field modal fun(modal_id: string, visible: boolean)
