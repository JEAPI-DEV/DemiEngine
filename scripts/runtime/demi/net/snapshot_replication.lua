local SnapshotReplication = {}
SnapshotReplication.__index = SnapshotReplication

local function noop()
end

local function default_log(message)
  if Debug and Debug.log then
    Debug.log(message)
  end
end

local function default_create_remote(session, ghost_id, snapshot)
  if session.remote_prefab == nil then
    return
  end

  local prefab = session.remote_prefab
  Entity.create(ghost_id, {
    name = prefab.name or "Network Ghost",
    components = {
      Transform2D = {
        position = { snapshot.x, snapshot.y },
        rotation = prefab.rotation or 0.0,
        scale = prefab.scale or { 1.0, 1.0 },
      },
      Sprite = {
        texture = prefab.texture,
        layer = prefab.layer or "network",
      },
    },
  })
end

local function default_update_remote(_session, ghost_id, _snapshot, predicted_x, predicted_y)
  Entity.set_position(ghost_id, predicted_x, predicted_y)
end

function SnapshotReplication.new(options)
  options = options or {}
  return setmetatable({
    send_interval = options.send_interval or (1.0 / 60.0),
    extrapolation_limit = options.extrapolation_limit or 0.10,
    initial_prediction = options.initial_prediction or 0.025,
    channel = options.channel or 1,
    default_port = options.port or 39420,
    max_peers = options.max_peers or 8,
    local_peer_id = nil,
    accumulator = 0.0,
    remotes = {},
    claim_once_objects = {},
    claim_once_claimed = {},
    session_metadata = nil,
    remote_prefab = options.remote_prefab,
    create_remote = options.create_remote or default_create_remote,
    update_remote = options.update_remote or default_update_remote,
    on_connected = options.on_connected or noop,
    on_disconnected = options.on_disconnected or noop,
    on_session_start = options.on_session_start or noop,
    on_message = options.on_message or noop,
    log = options.log or default_log,
  }, SnapshotReplication)
end

function SnapshotReplication:reset(clear_remote_entities)
  if clear_remote_entities then
    for ghost_id, _ in pairs(self.remotes) do
      Entity.destroy(ghost_id)
    end
  end
  self.local_peer_id = nil
  self.accumulator = 0.0
  self.remotes = {}
  self.claim_once_objects = {}
  self.claim_once_claimed = {}
  self.session_metadata = nil
end

function SnapshotReplication:available()
  return Network.available()
end

function SnapshotReplication:sender_id()
  return Network.sender_id(self.local_peer_id)
end

function SnapshotReplication:host(port)
  if not Network.available() then
    self.log("Network unavailable: rebuild with DEMI_ENABLE_NETWORK=ON to host.")
    return false
  end
  self:reset(true)
  return Network.host(port or self.default_port, self.max_peers)
end

function SnapshotReplication:connect(address, port)
  if not Network.available() then
    self.log("Network unavailable: rebuild with DEMI_ENABLE_NETWORK=ON to connect.")
    return false
  end
  self:reset(true)
  return Network.connect(address or "127.0.0.1", port or self.default_port)
end

function SnapshotReplication:disconnect()
  Network.disconnect()
  self:reset(true)
end

function SnapshotReplication:remote_id(snapshot)
  return "net_" .. snapshot.sender_id .. "_" .. snapshot.entity_id
end

function SnapshotReplication:remove_remotes_for_sender(sender_id)
  if sender_id == nil then
    return
  end

  for ghost_id, remote in pairs(self.remotes) do
    if remote.sender_id == sender_id then
      Entity.destroy(ghost_id)
      self.remotes[ghost_id] = nil
    end
  end
end

function SnapshotReplication:remote_position(sender_id)
  if sender_id == self:sender_id() then
    return nil, nil
  end

  local best_remote = nil
  for _, remote in pairs(self.remotes) do
    if remote.sender_id == sender_id and (best_remote == nil or remote.age < best_remote.age) then
      best_remote = remote
    end
  end

  if best_remote == nil then
    return nil, nil
  end
  return best_remote.x, best_remote.y
end

function SnapshotReplication:set_session_metadata(metadata)
  self.session_metadata = metadata
  if metadata ~= nil and Network.available() and Network.is_host() then
    Network.send(Network.encode("session_start", metadata), true, 0, 0)
  end
end

function SnapshotReplication:claim_once_sync_payload()
  local claims = {}
  for object_id, collector_id in pairs(self.claim_once_claimed) do
    claims[#claims + 1] = {
      object_id = object_id,
      collector_id = collector_id,
    }
  end
  return { claims = claims }
end

function SnapshotReplication:send_claim_once_sync(peer_id)
  if not Network.available() or not Network.is_host() then
    return false
  end
  return Network.send(Network.encode("claim_once_sync", self:claim_once_sync_payload()), true, peer_id or 0, 0)
end

function SnapshotReplication:request_claim_once_sync()
  if not Network.available() or Network.is_host() or not Network.is_connected() then
    return false
  end
  return Network.send(Network.encode("claim_once_sync_request", {}), true, 0, 0)
end

function SnapshotReplication:apply_snapshot(snapshot)
  if snapshot.sender_id == self:sender_id() or snapshot.x == nil or snapshot.y == nil then
    return
  end

  local ghost_id = self:remote_id(snapshot)
  if self.remotes[ghost_id] == nil then
    self.create_remote(self, ghost_id, snapshot)
    self.remotes[ghost_id] = {
      sender_id = snapshot.sender_id,
      x = snapshot.x,
      y = snapshot.y,
      vx = 0.0,
      vy = 0.0,
      age = 0.0,
    }
  end

  local remote = self.remotes[ghost_id]
  remote.x = snapshot.x
  remote.y = snapshot.y
  remote.vx = snapshot.vx or 0.0
  remote.vy = snapshot.vy or 0.0
  remote.age = 0.0
  self.update_remote(self, ghost_id, snapshot, remote.x + remote.vx * self.initial_prediction, remote.y + remote.vy * self.initial_prediction)
end

function SnapshotReplication:update_remotes(dt)
  for ghost_id, remote in pairs(self.remotes) do
    remote.age = math.min(remote.age + dt, self.extrapolation_limit)
    self.update_remote(self, ghost_id, remote, remote.x + remote.vx * remote.age, remote.y + remote.vy * remote.age)
  end
end

function SnapshotReplication:register_claim_once(id, options)
  options = options or {}
  local object = {
    claimed = self.claim_once_claimed[id] ~= nil,
    pending = false,
    on_removed = options.on_removed or function(object_id)
      Entity.destroy(object_id)
    end,
    on_claimed_local = options.on_claimed_local or noop,
    can_claim = options.can_claim,
  }
  self.claim_once_objects[id] = object

  if object.claimed then
    object.on_removed(id, self.claim_once_claimed[id])
    return false
  end
  return true
end

function SnapshotReplication:apply_claim_once(id, collector_id, broadcast, claim)
  if id == nil or self.claim_once_claimed[id] ~= nil then
    return false
  end

  local object = self.claim_once_objects[id]
  if broadcast and object ~= nil and object.can_claim ~= nil and not object.can_claim(id, collector_id, self, claim) then
    return false
  end

  self.claim_once_claimed[id] = collector_id
  if object ~= nil then
    object.claimed = true
    object.pending = false
    object.on_removed(id, collector_id)
    if collector_id == self:sender_id() then
      object.on_claimed_local(id, collector_id)
    end
  end

  if broadcast and Network.available() and Network.is_host() then
    Network.send(Network.encode("claim_once_claimed", {
      object_id = id,
      collector_id = collector_id,
    }), true)
  end
  return true
end

function SnapshotReplication:reject_claim_once(id, collector_id, peer_id)
  if id == nil then
    return false
  end

  local object = self.claim_once_objects[id]
  if object ~= nil then
    object.pending = false
  end

  if Network.available() and Network.is_host() and peer_id ~= nil then
    Network.send(Network.encode("claim_once_rejected", {
      object_id = id,
      collector_id = collector_id,
    }), true, peer_id, 0)
  end
  return true
end

function SnapshotReplication:try_claim_once(id, claim)
  local object = self.claim_once_objects[id]
  if object == nil or object.claimed or object.pending then
    return false
  end

  if not Network.available() or Network.is_host() or not Network.is_connected() then
    return self:apply_claim_once(id, self:sender_id(), true, claim)
  end

  object.pending = true
  claim = claim or {}
  claim.object_id = id
  return Network.send(Network.encode("claim_once_request", claim), true)
end

function SnapshotReplication:process_events()
  local summary = {
    connected = false,
    disconnected = false,
    session_started = false,
    session = nil,
    messages = 0,
  }

  if not Network.available() then
    return summary
  end

  for _, event in ipairs(Network.events()) do
    if event.type == "connected" then
      summary.connected = true
      self.log("Network peer connected: " .. tostring(event.peer_id))
      if Network.is_host() then
        Network.send(Network.encode("assign_peer", { peer_id = "peer" .. tostring(event.peer_id) }), true, event.peer_id, 0)
        if self.session_metadata ~= nil then
          Network.send(Network.encode("session_start", self.session_metadata), true, event.peer_id, 0)
        end
        self:send_claim_once_sync(event.peer_id)
      end
      self.on_connected(self, event)
    elseif event.type == "disconnected" then
      summary.disconnected = true
      self.log("Network peer disconnected: " .. tostring(event.peer_id))
      if Network.is_host() then
        self:remove_remotes_for_sender("peer" .. tostring(event.peer_id))
      else
        self:reset(true)
      end
      self.on_disconnected(self, event)
    elseif event.type == "message" then
      summary.messages = summary.messages + 1
      local message = Network.decode(event.message)
      if message ~= nil and message.type == "assign_peer" then
        self.local_peer_id = message.payload.peer_id
      else
        if message ~= nil and message.type == "transform_snapshot" then
          local snapshot = message.payload
          if Network.is_host() and snapshot.sender_id ~= "host" then
            snapshot.sender_id = "peer" .. tostring(event.peer_id)
            Network.send(Network.encode("transform_snapshot", snapshot), false, 0, self.channel)
          end
          self:apply_snapshot(snapshot)
        elseif message ~= nil and message.type == "claim_once_request" then
          if Network.is_host() and message.payload ~= nil then
            local object_id = message.payload.object_id
            local collector_id = "peer" .. tostring(event.peer_id)
            if self.claim_once_claimed[object_id] ~= nil then
              Network.send(Network.encode("claim_once_claimed", {
                object_id = object_id,
                collector_id = self.claim_once_claimed[object_id],
              }), true, event.peer_id, 0)
            elseif not self:apply_claim_once(object_id, collector_id, true, message.payload) then
              self:reject_claim_once(object_id, collector_id, event.peer_id)
            end
          end
        elseif message ~= nil and message.type == "claim_once_claimed" then
          if message.payload ~= nil then
            self:apply_claim_once(message.payload.object_id, message.payload.collector_id, false)
          end
        elseif message ~= nil and message.type == "claim_once_rejected" then
          if message.payload ~= nil then
            self:reject_claim_once(message.payload.object_id, message.payload.collector_id, nil)
          end
        elseif message ~= nil and message.type == "claim_once_sync" then
          if message.payload ~= nil and message.payload.claims ~= nil then
            for _, claim in pairs(message.payload.claims) do
              self:apply_claim_once(claim.object_id, claim.collector_id, false)
            end
          end
        elseif message ~= nil and message.type == "claim_once_sync_request" then
          if Network.is_host() then
            self:send_claim_once_sync(event.peer_id)
          end
        elseif message ~= nil and message.type == "session_start" then
          self.session_metadata = message.payload
          summary.session_started = true
          summary.session = message.payload
          self.on_session_start(self, message.payload)
        else
          self.on_message(self, event, message)
        end
      end
    end
  end
  return summary
end

function SnapshotReplication:send_transform(entity_id)
  local x, y = Entity.get_position(entity_id)
  if x == nil or y == nil then
    return false
  end

  local vx, vy = Rigidbody2D.get_velocity(entity_id)
  return Network.send(Network.encode("transform_snapshot", {
    sender_id = self:sender_id(),
    entity_id = entity_id,
    x = x,
    y = y,
    vx = vx or 0.0,
    vy = vy or 0.0,
  }), false, 0, self.channel)
end

function SnapshotReplication:update_entity(entity_id, dt)
  if not Network.available() then
    return
  end

  self:process_events()
  self:update_remotes(dt)

  if not Network.is_host() and not Network.is_connected() then
    return
  end

  self.accumulator = self.accumulator + dt
  if self.accumulator < self.send_interval then
    return
  end
  self.accumulator = 0.0
  self:send_transform(entity_id)
end

return SnapshotReplication
