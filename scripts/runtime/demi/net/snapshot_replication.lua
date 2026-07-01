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
    remote_prefab = options.remote_prefab,
    create_remote = options.create_remote or default_create_remote,
    update_remote = options.update_remote or default_update_remote,
    on_connected = options.on_connected or noop,
    on_disconnected = options.on_disconnected or noop,
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

function SnapshotReplication:apply_snapshot(snapshot)
  if snapshot.sender_id == self:sender_id() or snapshot.x == nil or snapshot.y == nil then
    return
  end

  local ghost_id = self:remote_id(snapshot)
  if self.remotes[ghost_id] == nil then
    self.create_remote(self, ghost_id, snapshot)
    self.remotes[ghost_id] = {
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

function SnapshotReplication:process_events()
  local summary = {
    connected = false,
    disconnected = false,
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
      end
      self.on_connected(self, event)
    elseif event.type == "disconnected" then
      summary.disconnected = true
      self.log("Network peer disconnected: " .. tostring(event.peer_id))
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
        else
          self.on_message(self, event)
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
