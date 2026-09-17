---@meta
-- Native module: require("demi.hud"). Annotations only.
---@class HudService
local Hud = {}
---@class HudNodeHandle
---@field id string
---@field generation integer
---@class HudVirtualLayout
local HudVirtualLayout = {}
---@return integer
function HudVirtualLayout:item_count() end
---@return number
function HudVirtualLayout:total_extent() end
---@param scroll_offset number
---@param viewport_extent number
---@param overscan? integer
---@return integer first One-based logical item index.
---@return integer count Number of live rows needed.
---@return number leading_extent Offset of the first returned item.
---@return number total_extent Total scrollable extent.
function HudVirtualLayout:visible_range(scroll_offset, viewport_extent, overscan) end
---@param index integer One-based logical item index.
---@param extent number
---@return boolean changed
---@return string error
function HudVirtualLayout:set_extent(index, extent) end
---@param index integer One-based logical item index.
---@return number offset
---@return string error
function HudVirtualLayout:item_offset(index) end
---@class HudNodeDefinition
---@field id string
---@field type? string
---@field text? string
---@field font? string Font2D asset ID; missing glyphs fall back to other loaded fonts.
---@field action? string
---@field style? string
---@field texture? string
---@field accessibility_label? string
---@field accessibility_description? string
---@field accessibility_hidden? boolean
---@field visible? boolean
---@field disabled? boolean
---@field focusable? boolean
---@field font_size? number
---@field x? number
---@field y? number
---@field width? number
---@field height? number
---@param id string
---@return HudNodeHandle|nil
function Hud.find(id) end
---@param parent string
---@param definition HudNodeDefinition
---@return HudNodeHandle|nil handle
---@return string error
function Hud.create(parent, definition) end
---@param source HudNodeHandle
---@param new_root_id string
---@param parent? string
---@return HudNodeHandle|nil handle
---@return string error
function Hud.clone(source, new_root_id, parent) end
---@param node HudNodeHandle
---@return boolean ok
---@return string error
function Hud.remove(node) end
---@param node HudNodeHandle
---@param parent string
---@return boolean ok
---@return string error
function Hud.reparent(node, parent) end
---@param parent string
---@return boolean ok
---@return string error
function Hud.clear_children(parent) end
---@param parent string
---@return string[]
function Hud.children(parent) end
---@alias HudAccessibilityRole "generic"|"group"|"static_text"|"image"|"button"|"check_box"|"slider"|"text_field"|"scroll_area"|"list"|"progress_bar"|"dialog"|"joystick"
---@class HudAccessibilityNode
---@field id string
---@field parent string
---@field role HudAccessibilityRole
---@field label string
---@field description string
---@field value_text string
---@field x number
---@field y number
---@field width number
---@field height number
---@field value number
---@field minimum number
---@field maximum number
---@field focused boolean
---@field disabled boolean
---@field checked boolean
---@field focusable boolean
---@field offscreen boolean
---@return HudAccessibilityNode[]
function Hud.accessibility_snapshot() end
---@param node HudNodeHandle
---@param property "opacity"|"x"|"y"|"scale"
---@param target number
---@param duration number
---@return integer handle
---@return string error
function Hud.tween(node, property, target, duration) end
---@param handle integer
---@return boolean
function Hud.cancel_tween(handle) end
---@param enabled boolean
function Hud.set_reduced_motion(enabled) end
---@param locale string
---@return boolean changed
---@return string error
function Hud.set_locale(locale) end
---@param enabled boolean
function Hud.set_pseudo_locale(enabled) end
---@param name string Variable declared by the active HUD document.
---@param value string
---@return boolean changed
---@return string error
function Hud.set_variable(name, value) end
---@param values table<string, string>
---@return boolean changed
---@return string error
function Hud.set_variables(values) end
---@param name string
---@return boolean
function Hud.has_variable(name) end
---@param name string
---@return string? value
function Hud.get_variable(name) end
function Hud.reset_variables() end
---@param item_count integer
---@param item_extent number
---@param scroll_offset number
---@param viewport_extent number
---@param overscan? integer
---@return integer first One-based logical item index.
---@return integer count Number of live rows needed.
function Hud.visible_range(item_count, item_extent, scroll_offset, viewport_extent, overscan) end
---@param item_extents number[]
---@return HudVirtualLayout|nil layout
---@return string error
function Hud.virtual_layout(item_extents) end
---@class HudRecycledRow
---@field key string Stable game-owned data key.
---@field index integer One-based logical row index.
---@field node HudNodeHandle Generation-checked live row root.
---@field offset number Logical offset in the collection.
---@field extent number Logical row extent.
---@field rebound boolean True when this pool slot was reset for a new key.
---@param collection_id string Stable owner ID for this recycler.
---@param row_template HudNodeHandle Hidden template node whose parent owns rows.
---@param keys string[] Stable unique row keys in display order.
---@param extents number[] Positive row extents matching keys.
---@param scroll_offset number
---@param viewport_extent number
---@param overscan? integer
---@return HudRecycledRow[] rows
---@return string error
function Hud.recycle_rows(collection_id, row_template, keys, extents, scroll_offset, viewport_extent, overscan) end
---@param collection_id string
---@return boolean cleared
function Hud.clear_recycled_rows(collection_id) end
---@return number width
---@return number height
function Hud.canvas_size() end
---@param id string
---@param text string
---@return boolean
function Hud.set_text(id, text) end
---@param id string
---@param font string Font2D asset ID, or an empty string for automatic fallback.
---@return boolean
function Hud.set_font(id, font) end
---@param id string
---@param size number
---@return boolean
function Hud.set_font_size(id, size) end
---@param id string
---@param x number
---@param y number
---@param width number
---@param height number
---@return boolean
function Hud.set_rect(id, x, y, width, height) end
---@param id string
---@param texture string
---@param source_x number
---@param source_y number
---@param source_width number
---@param source_height number
---@return boolean
function Hud.set_image(id, texture, source_x, source_y, source_width, source_height) end
---@param id string
---@param animation_id string
---@param frame integer
---@return boolean
function Hud.set_image_animation_frame(id, animation_id, frame) end
---@param id string
---@param x number
---@param y number
---@return boolean
function Hud.set_position(id, x, y) end
---@param id string
---@param width number
---@param height number
---@return boolean
function Hud.set_size(id, width, height) end
---@param id string
---@param r number
---@param g number
---@param b number
---@param a? number
---@return boolean
function Hud.set_color(id, r, g, b, a) end
---@param id string
---@param r number
---@param g number
---@param b number
---@param a? number
---@return boolean
function Hud.set_background_color(id, r, g, b, a) end
---@param id string
---@param opacity number
---@return boolean
function Hud.set_opacity(id, opacity) end
---@param id string
---@param visible boolean
---@return boolean
function Hud.set_visible(id, visible) end
---@param id string
---@param value number
---@return boolean
function Hud.set_value(id, value) end
---@param id string
---@param checked boolean
---@return boolean
function Hud.set_checked(id, checked) end
---@param id string
---@param disabled boolean
---@return boolean
function Hud.set_disabled(id, disabled) end
---@param reverse? boolean
---@return boolean
function Hud.focus_next(reverse) end
---@return string
function Hud.focused() end
---@param id string
---@return string|nil
function Hud.get_text(id) end

return Hud
