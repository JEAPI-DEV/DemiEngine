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
---@param action string
---@return boolean
function Input.action_down(action) end
---@param action string
---@return boolean
function Input.action_pressed(action) end
---@param action string
---@return number
function Input.action_value(action) end
---@return string
function Input.text_entered() end
---@param active boolean
function Input.set_text_input_active(active) end
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
---@param texture? string
---@return boolean
function ProceduralMesh.apply(entity_id, builder, texture) end

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
---@param entity_ids string[]
---@return integer
function Entity.destroy_many(entity_ids) end
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

---@class Sprite2DService
Sprite2D = {}
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

---@class TimeService
---@field delta_time number
Time = {}

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

---@class RuntimeService
Runtime = {}
function Runtime.quit() end
---@return "android"|"windows"|"macos"|"linux"|"unknown"
function Runtime.platform() end
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
---@class PhysicsRaycastHit2D
---@field entity_id string
---@field point number[]
---@field normal number[]
---@field distance number
---@param x number
---@param y number
---@param radius number
---@param layer? string
---@param ignored_entity_id? string
---@return string[]
function Physics2D.overlap_circle(x, y, radius, layer, ignored_entity_id) end
---@param origin_x number
---@param origin_y number
---@param direction_x number
---@param direction_y number
---@param distance number
---@param layer? string
---@param ignored_entity_id? string
---@return PhysicsRaycastHit2D|nil
function Physics2D.raycast(origin_x, origin_y, direction_x, direction_y, distance, layer, ignored_entity_id) end
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
---@param group string
---@param visible boolean
---@return boolean
function Hud.set_group_visible(group, visible) end
---@param id string
---@return string|nil
function Hud.get_text(id) end

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

---@class NetworkHttpResponse
---@field ok boolean
---@field status integer
---@field body string
---@field error string
---@field json table|nil

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
---@param options {send_interval?: number, extrapolation_limit?: number, initial_prediction?: number, channel?: integer, port?: integer, max_peers?: integer, remote_prefab?: table, certificate?: string, private_key?: string, trusted_certificate?: string, server_name?: string}
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
