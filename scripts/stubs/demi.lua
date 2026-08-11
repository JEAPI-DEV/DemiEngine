---@meta
-- Checked-in LuaLS/EmmyLua stubs copied by `demi lua-stubs generate`.

---@class DataError
---@field code string
---@field message string
---@field id string

---@class DataQuery
---@field content_type? string
---@field tags? string[]

---@class DataService
---@field null table JSON null sentinel; distinguish it with `Data.is_null`.
Data = {}
---@param id string Stable `asset://` ID.
---@return any? snapshot A detached Lua snapshot of the immutable document.
---@return DataError? error
function Data.load(id) end
---@param query? DataQuery
---@return table[] snapshots Ordered deterministically by stable asset ID.
function Data.query(query) end
---@param id string
---@return integer
function Data.revision(id) end
---@param value any
---@return "array"|"object"|"null"|nil
function Data.kind(value) end
---@param value any
---@return boolean
function Data.is_null(value) end

---@class DebugService
Debug = {}
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
---@param width? number
function Debug.line(x1, y1, x2, y2, r, g, b, a, width) end
function Debug.clear_lines() end

---@class ProfileService
Profile = {}
---@return boolean
function Profile.enabled() end
---@param name string
---@param callback fun()
---@return boolean
function Profile.scope(name, callback) end

---@class InputService
Input = {}
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

---@class ApplicationService
Application = {}
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

---@class ProceduralMeshBuilder
---@field clear fun(self: ProceduralMeshBuilder)
---@field reserve fun(self: ProceduralMeshBuilder, vertex_count: integer)
---@field vertex_count fun(self: ProceduralMeshBuilder): integer
---@field add_vertex fun(self: ProceduralMeshBuilder, x: number, y: number, z: number, nx: number, ny: number, nz: number, u: number, v: number)
---@field add_quad fun(self: ProceduralMeshBuilder, nx: number, ny: number, nz: number, x1: number, y1: number, z1: number, u1: number, v1: number, x2: number, y2: number, z2: number, u2: number, v2: number, x3: number, y3: number, z3: number, u3: number, v3: number, x4: number, y4: number, z4: number, u4: number, v4: number)
---@field add_voxel_blocks fun(self: ProceduralMeshBuilder, blocks: table, occupancy: table, block_tiles: table, atlas_columns: integer, occupancy_stride: integer)

---@class ProceduralMeshService
ProceduralMesh = {}
---@param capacity? integer
---@return ProceduralMeshBuilder
function ProceduralMesh.create(capacity) end
---@param entity_id string
---@param builder ProceduralMeshBuilder
---@param options? {texture?: string, material?: string, render_layer?: string}
---@return boolean
function ProceduralMesh.apply(entity_id, builder, options) end

---@class VoxelWorldHandle
---@field clear fun(self: VoxelWorldHandle)
---@field set_section fun(self: VoxelWorldHandle, cx: integer, section_y: integer, cz: integer, blocks: table)
---@field erase_section fun(self: VoxelWorldHandle, cx: integer, section_y: integer, cz: integer)
---@field build_section_mesh fun(self: VoxelWorldHandle, cx: integer, section_y: integer, cz: integer, block_tiles: table, atlas_columns: integer): ProceduralMeshBuilder

---@class VoxelWorldService
VoxelWorld = {}
---@param chunk_size integer
---@param section_height integer
---@return VoxelWorldHandle
function VoxelWorld.create(chunk_size, section_height) end

---@class EntityService
Entity = {}
---@class EntityQuery
---@field all? string[]
---@field tags? string[]
---@field layer? string
---@field include_disabled? boolean
---@param id_or_name string
---@return string|nil
function Entity.find(id_or_name) end
---@param entity_id string
---@return boolean
function Entity.exists(entity_id) end
---@param entity_id string
---@param spec table
---@return boolean
function Entity.create(entity_id, spec) end
---@param entity_id string
---@param spec table
---@return boolean
function Entity.replace(entity_id, spec) end
---@param source_id string
---@param new_id string
---@return boolean
function Entity.clone(source_id, new_id) end
---@param entity_id string
---@return boolean
function Entity.destroy(entity_id) end
---@param entity_ids string[]
---@return integer
function Entity.destroy_many(entity_ids) end
---@param entity_id string
---@param enabled boolean
---@return boolean
function Entity.set_enabled(entity_id, enabled) end
---@param entity_id string
---@return boolean
function Entity.is_enabled(entity_id) end
---@param entity_id string
---@param component string
---@param values table
---@return boolean
function Entity.add_component(entity_id, component, values) end
---@param entity_id string
---@param component string
---@return boolean
function Entity.remove_component(entity_id, component) end
---@param entity_id string
---@param component string
---@return boolean
function Entity.has_component(entity_id, component) end
---@param entity_id string
---@param component string
---@param field string
---@return any
function Entity.get(entity_id, component, field) end
---@param entity_id string
---@param component string
---@param field string
---@param value any
---@return boolean
function Entity.set(entity_id, component, field, value) end
---@param query EntityQuery
---@return string[]
function Entity.query(query) end
---@param entity_id string
---@param parent_id? string
---@return boolean
function Entity.set_parent(entity_id, parent_id) end
---@param entity_id string
---@return string|nil
function Entity.parent(entity_id) end
---@param entity_id string
---@return string[]
function Entity.children(entity_id) end
---@param entity_id string
---@return number[]|nil
function Entity.local_position(entity_id) end
---@param entity_id string
---@return number[]|nil
function Entity.world_position(entity_id) end

---@class TransformService
Transform = {}
---@param entity_id string
---@return number|nil x
---@return number|nil y
function Transform.get_position(entity_id) end
---@param entity_id string
---@param x number
---@param y number
---@return boolean
function Transform.set_position(entity_id, x, y) end
---@param entity_id string
---@param dx number
---@param dy number
---@return boolean
function Transform.add_position(entity_id, dx, dy) end
---@param entity_id string
---@return number|nil rotation
function Transform.get_rotation(entity_id) end
---@param entity_id string
---@param rotation number
---@return boolean
function Transform.set_rotation(entity_id, rotation) end
---@param entity_id string
---@return number|nil x
---@return number|nil y
function Transform.get_scale(entity_id) end
---@param entity_id string
---@param x number
---@param y number
---@return boolean
function Transform.set_scale(entity_id, x, y) end

---@class Transform3DService
Transform3D = {}
---@param entity_id string
---@return number|nil x
---@return number|nil y
---@return number|nil z
function Transform3D.get_position(entity_id) end
---@param entity_id string
---@param x number
---@param y number
---@param z number
---@return boolean
function Transform3D.set_position(entity_id, x, y, z) end
---@param entity_id string
---@param dx number
---@param dy number
---@param dz number
---@return boolean
function Transform3D.add_position(entity_id, dx, dy, dz) end
---@param entity_id string
---@return number|nil x
---@return number|nil y
---@return number|nil z
function Transform3D.get_rotation(entity_id) end
---@param entity_id string
---@param x number
---@param y number
---@param z number
---@return boolean
function Transform3D.set_rotation(entity_id, x, y, z) end
---@param entity_id string
---@return number|nil x
---@return number|nil y
---@return number|nil z
function Transform3D.get_scale(entity_id) end
---@param entity_id string
---@param x number
---@param y number
---@param z number
---@return boolean
function Transform3D.set_scale(entity_id, x, y, z) end
---@param entity_id string
---@return number|nil x
---@return number|nil y
---@return number|nil z
function Transform3D.forward(entity_id) end
---@param entity_id string
---@return number|nil x
---@return number|nil y
---@return number|nil z
function Transform3D.right(entity_id) end
---@param entity_id string
---@return number|nil x
---@return number|nil y
---@return number|nil z
function Transform3D.up(entity_id) end
---@param entity_id string
---@param x number
---@param y number
---@param z number
---@return boolean
function Transform3D.look_at(entity_id, x, y, z) end

---@class Sprite2DService
Sprite2D = {}
---@param entity_id string
---@param r number
---@param g number
---@param b number
---@param a? number
---@return boolean
function Sprite2D.set_color(entity_id, r, g, b, a) end
---@param entity_id string
---@param clip string
---@param restart? boolean
---@return boolean
function Sprite2D.play_animation(entity_id, clip, restart) end
---@param entity_id string
---@return boolean
function Sprite2D.pause_animation(entity_id) end
---@param entity_id string
---@return boolean
function Sprite2D.resume_animation(entity_id) end
---@param entity_id string
---@return string
function Sprite2D.current_animation(entity_id) end
---@param entity_id string
---@param flip_x boolean
---@param flip_y boolean
---@return boolean
function Sprite2D.set_flip(entity_id, flip_x, flip_y) end
---@param entity_id string
---@param width number
---@param height number
---@return boolean
function Sprite2D.set_size(entity_id, width, height) end
---@param entity_id string
---@param layer string
---@return boolean
function Sprite2D.set_layer(entity_id, layer) end
---@param entity_id string
---@param sorting_order integer
---@return boolean
function Sprite2D.set_sorting_order(entity_id, sorting_order) end
---@param entity_id string
---@param material string
---@return boolean
function Sprite2D.set_material(entity_id, material) end

---@class AnimationService
Animation = {}

---Returns the active named state, or an empty string when unavailable.
---@param entity_id string
---@return string
function Animation.state(entity_id) end

---Immediately enters a named animation state.
---@param entity_id string
---@param state string
---@return boolean
function Animation.play(entity_id, state) end

---@param entity_id string
---@param parameter string
---@param value number
---@return boolean
function Animation.set_number(entity_id, parameter, value) end

---@param entity_id string
---@param parameter string
---@param value boolean
---@return boolean
function Animation.set_bool(entity_id, parameter, value) end

---@param entity_id string
---@param trigger string
---@return boolean
function Animation.trigger(entity_id, trigger) end

---@param entity_id string
---@param speed number
---@return boolean
function Animation.set_speed(entity_id, speed) end

---@param entity_id string
---@return number
function Animation.normalized_time(entity_id) end

---@class AnimationTransitionInfo
---@field from string
---@field to string
---@field progress number
---@field active boolean

---@param entity_id string
---@return AnimationTransitionInfo
function Animation.transition(entity_id) end

---@param entity_id string
---@param layer string
---@param weight number
---@return boolean
function Animation.set_layer_weight(entity_id, layer, weight) end

---@param entity_id string
---@param enabled boolean
---@return boolean
function Animation.set_root_motion(entity_id, enabled) end

---@class TimeService
---@field delta_time number
---@field unscaled_delta_time number
---@field time number
---@field fixed_time number
---@field frame_count integer
---@field time_scale number
---@field paused boolean
Time = {}
---@param paused boolean
function Time.set_paused(paused) end
---@return boolean
function Time.is_paused() end
---@param scale number
function Time.set_scale(scale) end
---@return number
function Time.get_scale() end

---@class RandomService
Random = {}
---@param seed integer
function Random.seed(seed) end
---@return string Exact unsigned 64-bit state suitable for JSON saves.
function Random.state() end
---@param state string State previously returned by Random.state.
---@return boolean
function Random.restore(state) end
---@return number
function Random.value() end
---@param minimum number
---@param maximum number
---@return number
function Random.range(minimum, maximum) end
---@param minimum integer
---@param maximum integer
---@return integer
function Random.integer(minimum, maximum) end

---@class TimerService
Timer = {}
---@param seconds number
---@param callback fun(timer_id: integer)
---@return integer timer_id
function Timer.delay(seconds, callback) end
---@param seconds number
---@param callback fun(timer_id: integer)
---@return integer timer_id
function Timer.every(seconds, callback) end
---@param timer_id integer
---@return boolean
function Timer.cancel(timer_id) end

---@class EventsService
Events = {}
---@param event_name string
---@param callback fun(payload: table)
---@return integer subscription_id
function Events.subscribe(event_name, callback) end
---@param subscription_id integer
---@return boolean
function Events.unsubscribe(subscription_id) end
---@param event_name string
---@param payload? table
---@return integer delivered
function Events.emit(event_name, payload) end

---@class SceneService
Scene = {}
---@param scene_id string
---@return boolean
function Scene.load(scene_id) end
---@return boolean
function Scene.reload() end
---@param scene_id string
---@param additive? boolean
---@return boolean
function Scene.prepare(scene_id, additive) end
---@return boolean
function Scene.cancel() end
---@return number
function Scene.progress() end
---@return boolean
function Scene.is_prepared() end
---@return boolean
function Scene.activate() end
---@param scene_id string
---@return boolean
function Scene.unload(scene_id) end
---@param entity_id string
---@param persistent boolean
---@return boolean
function Scene.set_persistent(entity_id, persistent) end
---@return string
function Scene.active() end
---@return string
function Scene.error() end

---@class PrefabInstantiateOptions
---@field id string
---@field position? number[]
---@field overrides? table<string, table>
---@field pooled? boolean

---@class PrefabService
Prefab = {}
---@param prefab_id string
---@param options PrefabInstantiateOptions
---@return string|nil instance_id
function Prefab.instantiate(prefab_id, options) end
---@param instance_or_entity_id string
---@return boolean
function Prefab.release(instance_or_entity_id) end
---@param prefab_id string
---@return integer
function Prefab.pooled_count(prefab_id) end

---@class PhysicsService
Physics = {}
---@param enabled boolean
function Physics.set_enabled(enabled) end
---@return boolean
function Physics.enabled() end

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
---@param entity_id string
---@param x number
---@param y number
---@return boolean
function Rigidbody2D.add_force(entity_id, x, y) end
---@param entity_id string
---@param torque number
---@return boolean
function Rigidbody2D.add_torque(entity_id, torque) end
---@param entity_id string
---@param angular_velocity number
---@return boolean
function Rigidbody2D.set_angular_velocity(entity_id, angular_velocity) end
---@param entity_id string
---@param awake boolean
---@return boolean
function Rigidbody2D.set_awake(entity_id, awake) end
---@param entity_id string
---@param enabled boolean
---@return boolean
function Rigidbody2D.set_enabled(entity_id, enabled) end
---@param entity_id string
---@param x number
---@param y number
---@param fixed_dt? number
---@return boolean
function Rigidbody2D.move_kinematic(entity_id, x, y, fixed_dt) end
---@param entity_id string
---@param motion_x number
---@param motion_y number
---@return number?, number?
function Rigidbody2D.move_and_slide(entity_id, motion_x, motion_y) end

---@class Rigidbody3DService
Rigidbody3D = {}
---@param entity_id string
---@return number|nil x
---@return number|nil y
---@return number|nil z
function Rigidbody3D.get_velocity(entity_id) end
---@param entity_id string
---@param x number
---@param y number
---@param z number
---@return boolean
function Rigidbody3D.set_velocity(entity_id, x, y, z) end
---@param entity_id string
---@param x number
---@param y number
---@param z number
---@return boolean
function Rigidbody3D.add_force(entity_id, x, y, z) end
---@param entity_id string
---@param x number
---@param y number
---@param z number
---@return boolean
function Rigidbody3D.add_impulse(entity_id, x, y, z) end
---@param entity_id string
---@param x number
---@param y number
---@param z number
---@return boolean
function Rigidbody3D.add_torque(entity_id, x, y, z) end
---@param entity_id string
---@param awake boolean
---@return boolean
function Rigidbody3D.set_awake(entity_id, awake) end
---@param entity_id string
---@param enabled boolean
---@return boolean
function Rigidbody3D.set_enabled(entity_id, enabled) end
---@param entity_id string
---@param x number
---@param y number
---@param z number
---@param rotation_x number
---@param rotation_y number
---@param rotation_z number
---@param fixed_dt number
---@return boolean
function Rigidbody3D.move_kinematic(entity_id, x, y, z, rotation_x, rotation_y, rotation_z, fixed_dt) end

---@class CharacterController3DState
---@field velocity number[]
---@field grounded boolean
---@field ground_entity string
---@class CharacterController3DService
CharacterController3D = {}
---@param entity_id string
---@param x number
---@param y number
---@param z number
---@return boolean
function CharacterController3D.set_velocity(entity_id, x, y, z) end
---@param entity_id string
---@param speed number
---@return boolean
function CharacterController3D.jump(entity_id, speed) end
---@param entity_id string
---@return CharacterController3DState|nil
function CharacterController3D.state(entity_id) end

---@class CameraRay3D
---@field origin number[]
---@field direction number[]
---@class Camera3DService
Camera3D = {}
---@param entity_id string
---@param screen_x number
---@param screen_y number
---@param viewport_width number
---@param viewport_height number
---@return CameraRay3D|nil
function Camera3D.screen_ray(entity_id, screen_x, screen_y, viewport_width, viewport_height) end
---@param entity_id string
---@param world_x number
---@param world_y number
---@param world_z number
---@param viewport_width number
---@param viewport_height number
---@return number[]|nil
function Camera3D.world_to_screen(entity_id, world_x, world_y, world_z, viewport_width, viewport_height) end
---@param entity_id string
---@param screen_x number
---@param screen_y number
---@param viewport_width number
---@param viewport_height number
---@param distance number
---@return number|nil x
---@return number|nil y
---@return number|nil z
function Camera3D.screen_to_world(entity_id, screen_x, screen_y, viewport_width, viewport_height, distance) end

---@class Physics2DService
Physics2D = {}
---@param x number
---@param y number
---@param width number
---@param height number
---@param ignored_entity_id? string
---@return boolean
function Physics2D.overlap_box(x, y, width, height, ignored_entity_id) end
---@class PhysicsRaycastHit2D
---@field entity_id string
---@field layer string
---@field point number[]
---@field normal number[]
---@field distance number
---@field fraction number
---@param x number
---@param y number
---@param radius number
---@param layer? string
---@param ignored_entity_id? string
---@return string[]
function Physics2D.overlap_circle(x, y, radius, layer, ignored_entity_id) end
---@param x number
---@param y number
---@param width number
---@param height number
---@param layer? string
---@param ignored_entity_id? string
---@return PhysicsRaycastHit2D[]
function Physics2D.overlap_box_all(x, y, width, height, layer, ignored_entity_id) end
---@param x number
---@param y number
---@param radius number
---@param layer? string
---@param ignored_entity_id? string
---@return PhysicsRaycastHit2D[]
function Physics2D.overlap_circle_all(x, y, radius, layer, ignored_entity_id) end
---@param origin_x number
---@param origin_y number
---@param direction_x number
---@param direction_y number
---@param distance number
---@param layer? string
---@param ignored_entity_id? string
---@return PhysicsRaycastHit2D|nil
function Physics2D.raycast(origin_x, origin_y, direction_x, direction_y, distance, layer, ignored_entity_id) end

---@class Physics3DService
Physics3D = {}
---@class PhysicsRaycastHit3D
---@field entity_id string
---@field layer string
---@field point number[]
---@field normal number[]
---@field distance number
---@field fraction number
---@field is_trigger boolean
---@param x number
---@param y number
---@param z number
---@param radius number
---@param ignored_entity_id? string
---@return string[]
function Physics3D.overlap_sphere(x, y, z, radius, ignored_entity_id) end
---@param x number
---@param y number
---@param z number
---@param radius number
---@param layer? string
---@param ignored_entity_id? string
---@return PhysicsRaycastHit3D[]
function Physics3D.overlap_sphere_all(x, y, z, radius, layer, ignored_entity_id) end
---@param x number
---@param y number
---@param z number
---@param width number
---@param height number
---@param depth number
---@param layer? string
---@param ignored_entity_id? string
---@return PhysicsRaycastHit3D[]
function Physics3D.overlap_box_all(x, y, z, width, height, depth, layer, ignored_entity_id) end
---@param origin_x number
---@param origin_y number
---@param origin_z number
---@param direction_x number
---@param direction_y number
---@param direction_z number
---@param distance number
---@param ignored_entity_id? string
---@return PhysicsRaycastHit3D|nil
function Physics3D.raycast(origin_x, origin_y, origin_z, direction_x, direction_y, direction_z, distance, ignored_entity_id) end
---@param origin_x number
---@param origin_y number
---@param origin_z number
---@param radius number
---@param direction_x number
---@param direction_y number
---@param direction_z number
---@param distance number
---@param layer? string
---@param ignored_entity_id? string
---@return PhysicsRaycastHit3D|nil
function Physics3D.sphere_cast(origin_x, origin_y, origin_z, radius, direction_x, direction_y, direction_z, distance, layer, ignored_entity_id) end

---@class PhysicsContactFilter2D
---@field layer? string
---@field normal_x_min? number
---@field normal_x_max? number
---@field normal_y_min? number
---@field normal_y_max? number
---@field include_triggers? boolean
---@param entity_id string
---@param filter? PhysicsContactFilter2D
---@return boolean
function Physics2D.has_contact(entity_id, filter) end
---@class PhysicsContact2D
---@field entity_id string
---@field other_entity_id string
---@field other_layer string
---@field phase "enter"|"stay"|"exit"
---@field point number[]
---@field normal_x number
---@field normal_y number
---@field normal_impulse number
---@field is_trigger boolean
---@param entity_id string
---@return PhysicsContact2D[]
function Physics2D.contacts(entity_id) end

---@class Tilemap2DService
Tilemap2D = {}
---@param entity_id string
---@param layer string
---@param column integer
---@param row integer
---@return integer|nil
function Tilemap2D.get_tile(entity_id, layer, column, row) end
---@param entity_id string
---@param layer string
---@param column integer
---@param row integer
---@param tile integer
---@return boolean
function Tilemap2D.set_tile(entity_id, layer, column, row, tile) end
---@param entity_id string
---@return boolean
function Tilemap2D.clear_overrides(entity_id) end
---@param entity_id string
---@return boolean
function Tilemap2D.bake_navigation(entity_id) end
---@class TilemapObject2D
---@field id string
---@field type string
---@field x number
---@field y number
---@field width number
---@field height number
---@field properties table
---@param entity_id string
---@param layer string
---@return TilemapObject2D[]
function Tilemap2D.objects(entity_id, layer) end

---@class Navigation2DService
Navigation2D = {}
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

---@class HudService
Hud = {}
---@class HudNodeHandle
---@field id string
---@field generation integer
---@class HudVirtualLayout
HudVirtualLayout = {}
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

---@class TextLayoutLine
---@field text string
---@field x number
---@field y number
---@field width number
---@class TextLayoutResult
---@field width number
---@field height number
---@field grapheme_count integer
---@field truncated boolean
---@field valid_utf8 boolean
---@field shaping_complete boolean
---@field lines TextLayoutLine[]
---@class TextService
Text = {}
---@param value string
---@return integer
function Text.grapheme_count(value) end
---@param value string
---@param first integer One-based grapheme index.
---@param count integer
---@return string|nil
function Text.grapheme_slice(value, first, count) end
---@param value string
---@param width number
---@param font_size number
---@param max_lines? integer
---@return TextLayoutResult
function Text.layout(value, width, font_size, max_lines) end
---@class RichTextSpan
---@field begin integer
---@field length integer
---@field style string
---@field value string
---@class RichTextResult
---@field text string
---@field spans RichTextSpan[]
---@field diagnostics string[]
---@param markup string
---@param strict? boolean
---@return RichTextResult
function Text.parse_rich(markup, strict) end

---@class RegexService
Regex = {}
---@param pattern string
---@return boolean
function Regex.is_valid(pattern) end
---@param value string
---@param pattern string ECMAScript regular expression
---@param case_sensitive? boolean Defaults to false
---@return boolean
function Regex.matches(value, pattern, case_sensitive) end

---@class SaveService
Save = {}
---@param slot string
---@param key string
---@param fallback? number
---@return number
function Save.get_number(slot, key, fallback) end
---@param slot string
---@param key string
---@param value number
---@return boolean
function Save.set_number(slot, key, value) end
---@param slot string
---@param key string
---@param fallback? string
---@return string
function Save.get_string(slot, key, fallback) end
---@param slot string
---@param key string
---@param value string
---@return boolean
function Save.set_string(slot, key, value) end
---@param slot string
---@return table|nil
function Save.read(slot) end
---@param slot string
---@param state table
---@param format_version? integer
---@return boolean
function Save.write(slot, state, format_version) end

---@class GameSaveState
---@field game table<string, any>
---@field selected_entities table<string, any>
---@field prefab_instances table<string, any>
---@field lua table<string, any>

---@class GameSaveOptions
---@field format_version? integer
---@field autosave? boolean
---@field sequence? integer
---@field reason? string

---@class GameSaveMetadata
---@field autosave boolean
---@field sequence integer
---@field reason string

---@param slot string
---@param state GameSaveState
---@param options? GameSaveOptions
---@return boolean
function Save.write_state(slot, state, options) end

---@param slot string
---@return GameSaveState?
function Save.read_state(slot) end

---@param slot string
---@return GameSaveMetadata?
function Save.metadata(slot) end

---@return string
function Save.last_error() end
---@param slot string
---@return boolean
function Save.exists(slot) end
---@param slot string
---@return boolean
function Save.delete(slot) end
---@param slot string
---@return integer
function Save.version(slot) end
---@param from_version integer
---@param to_version integer
---@param callback fun(state: table, from_version: integer, to_version: integer): table
---@return integer migration_id
function Save.register_migration(from_version, to_version, callback) end

---@class AudioService
Audio = {}
---@class AudioPlayOptions
---@field bus? '"music"'|'"sfx"'|'"voice"'|'"ui"'|string
---@field loop? boolean
---@field streaming? boolean
---@field volume? number
---@field pitch? number
---@field pan? number
---@field spatial? '"none"'|'"2d"'|'"3d"'
---@field attenuation? '"none"'|'"inverse"'|'"linear"'|'"exponential"'
---@field x? number
---@field y? number
---@field z? number
---@field min_distance? number
---@field max_distance? number
---@field rolloff? number
---@field doppler? boolean
---@field delay? number
---@field fade_in? number
---@field concurrency_group? string
---@field max_voices? integer
---@field voice_stealing? '"reject"'|'"oldest"'|'"quietest"'
---@field pause_with_game? boolean
---@param asset_id string
---@param options? AudioPlayOptions
---@return integer handle
function Audio.play(asset_id, options) end
---@param handle integer
---@return boolean
function Audio.stop(handle) end
---@param volume number
function Audio.set_master_volume(volume) end
---@return number
function Audio.get_master_volume() end
---@param bus string
---@param volume number
---@return boolean
function Audio.set_bus_volume(bus, volume) end
---@param bus string
---@return number
function Audio.get_bus_volume(bus) end
---@param bus string
---@param muted boolean
---@return boolean
function Audio.set_bus_muted(bus, muted) end
---@param bus string
---@param paused boolean
---@return boolean
function Audio.set_bus_paused(bus, paused) end
---@param name string
---@param volumes table<string, number>
function Audio.define_snapshot(name, volumes) end
---@param name string
---@param duration number
---@return boolean
function Audio.transition_snapshot(name, duration) end
---@param from_handle integer
---@param asset_id string
---@param duration number
---@param bus? string
---@param loop? boolean
---@param streaming? boolean
---@return integer handle
function Audio.crossfade(from_handle, asset_id, duration, bus, loop, streaming) end

---@class AudioSourceService
AudioSource = {}
---@param entity_id string
---@return integer handle
function AudioSource.play(entity_id) end
---@param entity_id string
---@return boolean
function AudioSource.stop(entity_id) end

---@class VideoService
Video = {}
---@param asset_id string
---@param loop? boolean
---@return integer handle
function Video.play(asset_id, loop) end
---@param entity_id string
---@return integer handle
function Video.play_component(entity_id) end
---@param handle integer
---@return boolean
function Video.stop(handle) end
---@param handle integer
---@return boolean
function Video.is_playing(handle) end

---@class CutsceneService
Cutscene = {}
---@param id string
---@return boolean
function Cutscene.play(id) end
---@return boolean
function Cutscene.pause() end
---@return boolean
function Cutscene.resume() end
---@return boolean
function Cutscene.skip() end
---@return boolean
function Cutscene.stop() end
---@return boolean
function Cutscene.is_playing() end
---@return string
function Cutscene.active() end

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
Grid = {}
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

---@class NetworkService
Network = {}
---@return boolean
function Network.available() end
---@param port integer
---@param max_peers? integer
---@return boolean
function Network.host(port, max_peers) end
---@param port integer
---@param certificate string
---@param private_key string
---@param max_peers? integer
---@return boolean
function Network.host_dtls(port, certificate, private_key, max_peers) end
---@param address string
---@param port integer
---@return boolean
function Network.connect(address, port) end
---@param address string
---@param port integer
---@param trusted_certificate string
---@param server_name? string
---@return boolean
function Network.connect_dtls(address, port, trusted_certificate, server_name) end
function Network.disconnect() end
---@param peer_id integer
function Network.disconnect_peer(peer_id) end
---@param message string
---@param reliable? boolean
---@param peer_id? integer
---@param channel? integer
---@return boolean
function Network.send(message, reliable, peer_id, channel) end
---@return boolean
function Network.is_host() end
---@return boolean
function Network.is_connected() end
---@return boolean
function Network.is_secure() end
---@return string
function Network.security_error() end
---@return integer
function Network.latency_ms() end
---@return NetworkEvent[]
function Network.events() end
---@param url string
---@param timeout_ms? integer
---@return NetworkHttpResponse
function Network.http_get(url, timeout_ms) end
---@param url string
---@param fields table<string, string|number|boolean>
---@param timeout_ms? integer
---@return NetworkHttpResponse
function Network.http_post_form(url, fields, timeout_ms) end
---@param url string
---@param game? string
---@param timeout_ms? integer
---@return NetworkHttpResponse
function Network.lobby_list(url, game, timeout_ms) end
---@param url string
---@param game string|nil
---@param port integer
---@param player_name? string
---@param timeout_ms? integer
---@return NetworkHttpResponse
function Network.lobby_create(url, game, port, player_name, timeout_ms) end
---@param url string
---@param lobby_id integer
---@param player_name? string
---@param timeout_ms? integer
---@return NetworkHttpResponse
function Network.lobby_join(url, lobby_id, player_name, timeout_ms) end
---@param url string
---@param lobby_id integer
---@param player_token string
---@param timeout_ms? integer
---@return NetworkHttpResponse
function Network.lobby_heartbeat(url, lobby_id, player_token, timeout_ms) end
---@param url string
---@param lobby_id integer
---@param player_token string
---@param timeout_ms? integer
---@return NetworkHttpResponse
function Network.lobby_leave(url, lobby_id, player_token, timeout_ms) end
---@param assigned_peer_id? string
---@return string
function Network.sender_id(assigned_peer_id) end
---@param type string
---@param payload? table
---@return string
function Network.encode(type, payload) end
---@param message string
---@return table|nil
function Network.decode(message) end

---@class TlsEvent
---@field type "connected"|"disconnected"|"message"
---@field client_id integer
---@field message string

---@class TlsServerService
TlsServer = {}
---@param port integer
---@param certificate string
---@param private_key string
---@param max_clients? integer
---@return boolean
function TlsServer.listen(port, certificate, private_key, max_clients) end
---@return TlsEvent[]
function TlsServer.events() end
---@param client_id integer
---@param message string
---@return boolean
function TlsServer.send(client_id, message) end
---@param client_id integer
function TlsServer.disconnect(client_id) end
---@return string
function TlsServer.error() end

---@class TlsClientService
TlsClient = {}
---@param host string
---@param port integer
---@param trusted_certificate string
---@param server_name? string
---@return boolean
function TlsClient.connect(host, port, trusted_certificate, server_name) end
---@return TlsEvent[]
function TlsClient.events() end
---@param message string
---@return boolean
function TlsClient.send(message) end
function TlsClient.disconnect() end
---@return boolean
function TlsClient.is_connected() end
---@return string
function TlsClient.error() end

---@class CryptoService
Crypto = {}
---@param bytes? integer
---@return string
function Crypto.random_token(bytes) end
---@param password string
---@param salt string
---@param iterations? integer
---@return string
function Crypto.password_hash(password, salt, iterations) end
---@param left string
---@param right string
---@return boolean
function Crypto.secure_equals(left, right) end

---@class NetworkSessionService
NetworkSession = {}
---@class NetworkSessionDiagnostics
---@field mode "offline"|"host"|"client"
---@field local_peer_id string
---@field connected boolean
---@field secure boolean
---@field latency_ms integer
---@field connected_peers integer
---@field sent_messages integer
---@field received_messages integer
---@field rejected_messages integer
---@field session_epoch integer
---@field contract_hash string
---@field secure_accepted_messages integer
---@field secure_rejected_messages integer
---@field secure_ready boolean
---@field phase "closed"|"connected"|"authenticated"|"ready"|"active"|"reconnecting"
---@field last_error string
---@class NetworkContractInfo
---@field active boolean
---@field id string
---@field compatibility_hash string
---@field maximum_message_bytes integer
---@field maximum_owned_entities_per_peer integer
---@class NetworkSessionGameEvent
---@field name string
---@field sender_id string
---@field data any
---@class NetworkSessionUpdate
---@field connected boolean
---@field disconnected boolean
---@field session_started boolean
---@field session table|nil
---@field messages integer
---@field events NetworkSessionGameEvent[]
---@class NetworkRemotePrefab
---@field name? string
---@field texture? string
---@field shape? "rectangle"|"circle"|"triangle"
---@field layer? string
---@field sorting_order? integer
---@field size? number[]
---@field pivot? number[]
---@field parent? string
---@field rotation? number
---@field scale? number[]
---@field color? number[]
---@param options {send_interval?: number, extrapolation_limit?: number, initial_prediction?: number, channel?: integer, port?: integer, max_peers?: integer, remote_prefab?: NetworkRemotePrefab, certificate?: string, private_key?: string, trusted_certificate?: string, server_name?: string}
function NetworkSession.configure(options) end
---@return string
function NetworkSession.sender_id() end
---@return boolean
function NetworkSession.is_host() end
---@return NetworkSessionDiagnostics
function NetworkSession.diagnostics() end
---@return NetworkContractInfo
function NetworkSession.contract() end
---@param network_id string
---@return string|nil
function NetworkSession.owner(network_id) end
---@param network_id string
---@return boolean
function NetworkSession.has_authority(network_id) end
---@deprecated Use transfer with a declared network contract. Clients cannot change ownership.
---@param network_id string
---@param owner string
---@return boolean
function NetworkSession.set_authority(network_id, owner) end
---@deprecated For networking, use NetworkSession.send with a declared message. Events.emit remains the supported local event bus.
---@param name string
---@param data? any
---@param reliable? boolean
---@return boolean
function NetworkSession.emit(name, data, reliable) end
---@deprecated Use spawn with a prefab declared by the active network contract.
---@param entity_id string
---@param options? {network_id?: string, owner?: string}
---@return boolean
function NetworkSession.register_entity(entity_id, options) end
---@param name string Declared contract message name.
---@param target? string Network entity target when required by the contract.
---@param data? any Payload validated against the declared schema.
---@return boolean
function NetworkSession.send(name, target, data) end
---@param prefab_key string Key from network_contract.replicated_prefabs.
---@param entity_id string Local scene entity represented by this network entity.
---@param owner? string Authenticated peer ID; server-only.
---@return string|nil network_id
function NetworkSession.spawn(prefab_key, entity_id, owner) end
---@param network_id string
---@param owner string
---@return boolean
function NetworkSession.transfer(network_id, owner) end
---@param owner string
---@return string|nil network_id
function NetworkSession.network_id_for_owner(owner) end
---@param network_id string
---@return boolean
function NetworkSession.despawn(network_id) end
---@param r number
---@param g number
---@param b number
---@param a? number
function NetworkSession.set_local_color(r, g, b, a) end
---@param port? integer
---@return boolean
function NetworkSession.host(port) end
---@param address? string
---@param port? integer
---@return boolean
function NetworkSession.connect(address, port) end
function NetworkSession.disconnect() end
---@return boolean
function NetworkSession.is_connected() end
---@param metadata table
function NetworkSession.start_session(metadata) end
---@return table|nil
function NetworkSession.current_session() end
function NetworkSession.reset_claims() end
---@param sender_id string
---@return number|nil x
---@return number|nil y
function NetworkSession.remote_position(sender_id) end
---@param id string
---@param options? table
---@return boolean
function NetworkSession.register_claim_once(id, options) end
---@param id string
---@param collector_id string
---@param broadcast? boolean
---@param claim? table
---@return boolean
function NetworkSession.apply_claim_once(id, collector_id, broadcast, claim) end
---@param peer_id? integer
---@return boolean
function NetworkSession.request_claim_once_sync(peer_id) end
---@param id string
---@param claim? table
---@return boolean
function NetworkSession.try_claim_once(id, claim) end
---@return NetworkSessionUpdate
function NetworkSession.process_events() end
---@param network_id string
---@param dt number
---@return boolean
function NetworkSession.update_entity(network_id, dt) end

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
