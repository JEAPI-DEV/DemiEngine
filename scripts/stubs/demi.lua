---@meta
-- Checked-in LuaLS/EmmyLua stubs copied by `demi lua-stubs generate`.

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
function Debug.line(x1, y1, x2, y2, r, g, b, a) end
function Debug.clear_lines() end

---@class InputService
Input = {}
---@param key string
---@return boolean
function Input.is_down(key) end
---@param key string
---@return boolean
function Input.is_pressed(key) end
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

---@class EntityService
Entity = {}
---@param id_or_name string
---@return string|nil
function Entity.find(id_or_name) end
---@param entity_id string
---@param spec table
---@return boolean
function Entity.create(entity_id, spec) end
---@param entity_id string
---@return boolean
function Entity.destroy(entity_id) end
---@param entity_id string
---@param r number
---@param g number
---@param b number
---@param a? number
---@return boolean
function Entity.set_sprite_color(entity_id, r, g, b, a) end
---@param entity_id string
---@param dx number
---@param dy number
function Entity.add_position(entity_id, dx, dy) end
---@param entity_id string
---@param x number
---@param y number
---@return boolean
function Entity.set_position(entity_id, x, y) end
---@param entity_id string
---@return number|nil x
---@return number|nil y
function Entity.get_position(entity_id) end

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

---@class TimeService
---@field delta_time number
Time = {}

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

---@class RuntimeService
Runtime = {}
function Runtime.quit() end
---@param enabled boolean
function Runtime.set_physics_enabled(enabled) end
---@param mode string
function Runtime.set_window_mode(mode) end
---@return string mode
function Runtime.get_window_mode() end
---@param max_fps number
function Runtime.set_max_fps(max_fps) end
---@return integer max_fps
function Runtime.get_max_fps() end
---@param captured boolean
function Runtime.set_mouse_captured(captured) end
---@return boolean captured
function Runtime.get_mouse_captured() end

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
---@field normal_x number
---@field normal_y number
---@field is_trigger boolean
---@param entity_id string
---@return PhysicsContact2D[]
function Physics2D.contacts(entity_id) end

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
---@param scale number
---@return boolean
function Hud.set_text_scale(id, scale) end
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
---@param label string
---@return boolean
function Hud.set_button_label(id, label) end
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
---@param asset_id string
---@return integer handle
function Audio.play(asset_id) end
---@param handle integer
---@return boolean
function Audio.stop(handle) end
---@param volume number
function Audio.set_master_volume(volume) end
---@return number
function Audio.get_master_volume() end

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

---@class NetworkService
Network = {}
---@return boolean
function Network.available() end
---@param port integer
---@param max_peers? integer
---@return boolean
function Network.host(port, max_peers) end
---@param address string
---@param port integer
---@return boolean
function Network.connect(address, port) end
function Network.disconnect() end
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
---@return integer
function Network.latency_ms() end
---@return NetworkEvent[]
function Network.events() end
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

---@class NetworkSessionService
NetworkSession = {}
---@param options table
function NetworkSession.configure(options) end
---@return string
function NetworkSession.sender_id() end
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
---@return table
function NetworkSession.process_events() end
---@param entity_id string
---@param dt number
function NetworkSession.update_local_transform(entity_id, dt) end

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

---@class DemiUiEvent
---@field id string
---@field label string
---@field mouse_x number
---@field mouse_y number
---@param event DemiUiEvent
function DemiScript:on_ui_hover(event) end
---@param event DemiUiEvent
function DemiScript:on_ui_click(event) end
