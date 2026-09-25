---@meta
-- Native module: require("demi.network.session"). Annotations only.
---@class NetworkSessionService
local NetworkSession = {}
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
---@param options {send_interval?: number, extrapolation_limit?: number, initial_prediction?: number, interpolation_delay?: number, snapshot_buffer?: integer, input_queue_capacity?: integer, input_future_window?: integer, input_head_of_line_timeout?: number, input_max_per_tick?: integer, prediction_history_limit?: integer, prediction_visual_decay?: number, query_history_capacity?: integer, query_history_max_entities?: integer, query_history_rewind_ticks?: integer, port?: integer, max_peers?: integer, remote_prefab?: NetworkRemotePrefab, certificate?: string, private_key?: string, trusted_certificate?: string, server_name?: string}
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
---@param sender_id string
---@return number|nil x
---@return number|nil y
function NetworkSession.remote_position(sender_id) end
---@return NetworkSessionUpdate
function NetworkSession.process_events() end
---@param network_id string
---@param dt number
---@return boolean
function NetworkSession.update_entity(network_id, dt) end
---Associates an authoritative spawn owned by this peer with its local scene
---entity. Ownership and entity existence are validated by the runtime.
---@param network_id string
---@param entity_id string
---@return boolean
function NetworkSession.bind_local_entity(network_id, entity_id) end

---@class NetworkPredictionOptions
---@field network_id string Network entity the local peer owns and predicts.
---@field state table Serializable initial controller state; gameplay-defined.
---@field apply fun(state: table, input: table): table Deterministic replay callback returning the new state.
---@field input_message string Declared contract message used to carry inputs to the server.
---@class NetworkSnapshotPublishOptions
---@field marker? "normal"|"teleport"|"reset" Correction marker; reset and teleport clear client history.
---@class NetworkPredictionServerDiagnostics
---@field last_acked integer
---@field pending integer
---@field accepted integer
---@field rejected_old integer
---@field rejected_duplicate integer
---@field rejected_future integer
---@field rejected_capacity integer
---@field rejected_malformed integer
---@field discarded_gaps integer
---@field discarded_rejected integer
---@class NetworkInterpolationDiagnostics
---@field buffer_depth integer
---@field accepted integer
---@field dropped_stale integer
---@field dropped_overflow integer
---@field cleared_for_epoch integer
---@field cleared_for_generation integer
---@field interpolated integer
---@field extrapolated integer
---@field clamped integer
---@field snapped integer
---@class NetworkPredictionChannelDiagnostics
---@field prediction_enabled boolean
---@field input_message string
---@field next_sequence integer
---@field pending_replay integer
---@field corrections integer
---@field replayed_commands integer
---@field discarded_inputs integer
---@field dropped_history integer
---@field snaps integer
---@field rebases integer
---@field ownership_changes integer
---@field stale_snapshots integer
---@field last_correction_distance number
---@field last_divergence number
---@field visual_offset table<string, number>
---@field server NetworkPredictionServerDiagnostics
---@field interpolation NetworkInterpolationDiagnostics
---@class NetworkPredictionDiagnostics
---@field channels table<string, NetworkPredictionChannelDiagnostics>
---Evaluates queued owner inputs in sequence order at the authoritative fixed
---tick. Rejected, duplicate, stale, and discarded-gap inputs advance the
---acknowledgment without reaching gameplay. Host with a network contract only.
---@param network_id string
---@return table inputs Ordered accepted inputs, each carrying its seq field.
function NetworkSession.take_inputs(network_id) end
---Publishes an authoritative controller-state snapshot carrying the session
---epoch, ownership generation, server tick, last-evaluated input sequence,
---and a bounded sanitized rejection code. Host with a network contract only.
---@param network_id string
---@param state table Serializable authoritative controller state.
---@param options? NetworkSnapshotPublishOptions
---@return boolean
function NetworkSession.publish_snapshot(network_id, state, options) end
---Enables opt-in local prediction for a contract entity owned by the connected client. The apply callback
---must be deterministic and define the replayable controller state.
---@param options NetworkPredictionOptions
---@return boolean
function NetworkSession.enable_prediction(options) end
---@param network_id string
---@return boolean
function NetworkSession.disable_prediction(network_id) end
---Re-bases predicted state after a scene transition without restarting the
---input sequence space within the current session epoch.
---@param network_id string
---@param state table
---@return boolean
function NetworkSession.reset_prediction(network_id, state) end
---Applies one input immediately to the predicted state, records it for
---replay, and sends it through the declared input message when connected.
---@param network_id string
---@param input table
---@return integer|nil sequence
function NetworkSession.predict_input(network_id, input) end
---@param network_id string
---@return table|nil state Current reconciled and replayed controller state.
function NetworkSession.prediction_state(network_id) end
---@param network_id string
---@return table<string, number>|nil offset Render-only decaying visual offset.
function NetworkSession.prediction_visual_offset(network_id) end
---Returns the interpolated authoritative state for a non-owned entity.
---@param network_id string
---@return table|nil state
function NetworkSession.remote_state(network_id) end
---@return NetworkPredictionDiagnostics
function NetworkSession.prediction_diagnostics() end

---@class NetworkHistoricalCircle2D
---@field entity_id string Stable network entity ID.
---@field layer? string Optional query layer.
---@field x number
---@field y number
---@field radius number
---@class NetworkHistoricalRaycastHit2D
---@field entity_id string
---@field layer string
---@field sampled_tick integer
---@field x number
---@field y number
---@field normal_x number
---@field normal_y number
---@field distance number
---@class NetworkQueryHistoryDiagnostics2D
---@field depth integer
---@field latest_tick integer
---@field recorded integer
---@field rejected integer
---@field dropped integer
---@field queries integer
---@field clamped_queries integer
---@field misses integer
---Records selected authoritative collision circles without mutating the live
---world. Host with a network contract only; bounds are configured explicitly.
---@param server_tick integer
---@param circles NetworkHistoricalCircle2D[]
---@return boolean
function NetworkSession.record_query_snapshot(server_tick, circles) end
---Raycasts a bounded historical snapshot for server-side lag compensation.
---@param requested_tick integer
---@param origin_x number
---@param origin_y number
---@param direction_x number
---@param direction_y number
---@param maximum_distance number
---@param layer? string
---@param ignored_entity_id? string
---@return NetworkHistoricalRaycastHit2D|nil
function NetworkSession.historical_raycast(requested_tick, origin_x, origin_y, direction_x, direction_y, maximum_distance, layer, ignored_entity_id) end
---@return NetworkQueryHistoryDiagnostics2D
function NetworkSession.query_history_diagnostics() end
---Clears detached query history during an authoritative scene/reset boundary.
---@return boolean
function NetworkSession.clear_query_history() end

return NetworkSession
