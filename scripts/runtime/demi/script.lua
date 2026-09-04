-- demi.script: per-instance self-bound helpers + auto-cleanup subscriptions.
-- require("demi.script") then Script.bind(self) inside on_create.
-- No engine changes: desugars to existing Entity/Transform/Hud/Events/Timer.
---@class DemiScriptSelf
local Script = {}

local function entity_id(self)
  local id = self and self.entity_id
  assert(id and id ~= "", "demi.script: self.entity_id is not set")
  return id
end

---Wrap a script table so self:move(), self:on(), self:after() work and all
---subscriptions/timers are released when release() is called (call from
---on_destroy). Returns self for chaining.
---@param self table script instance table
---@return table self
function Script.bind(self)
  if self and self.__demi_bound then
    return self
  end
  self = self or {}
  self.__demi_bound = true
  self.__demi_subscriptions = {}
  self.__demi_timers = {}
  return self
end

local function track_sub(self, id)
  if self.__demi_subscriptions then
    table.insert(self.__demi_subscriptions, id)
  end
  return id
end

local function track_timer(self, id)
  if self.__demi_timers then
    table.insert(self.__demi_timers, id)
  end
  return id
end

---Release all subscriptions/timers created via self:on()/self:after().
---Call from on_destroy.
---@param self table
function Script.release(self)
  if not self then
    return
  end
  if self.__demi_subscriptions then
    for _, id in ipairs(self.__demi_subscriptions) do
      Events.unsubscribe(id)
    end
    self.__demi_subscriptions = {}
  end
  if self.__demi_timers then
    for _, id in ipairs(self.__demi_timers) do
      Timer.cancel(id)
    end
    self.__demi_timers = {}
  end
end

---Subscribe to a local event; auto-released via Script.release(self).
---@param self table
---@param event_name string
---@param callback fun(payload: table)
---@return integer subscription_id
function Script.on(self, event_name, callback)
  return track_sub(self, Events.subscribe(event_name, callback))
end

---One-shot timer bound to this script; auto-cancelled via Script.release.
---@param self table
---@param seconds number
---@param callback fun(timer_id: integer)
---@return integer timer_id
function Script.after(self, seconds, callback)
  return track_timer(self, Timer.after(seconds, callback))
end

---Spawn an entity with position/velocity shorthand + optional ttl cleanup.
---@param self table
---@param entity_id string
---@param options table EntitySpawnOptions (position/velocity/ttl/components)
---@return boolean ok
function Script.spawn(self, entity_id, options)
  options = options or {}
  local ok = Entity.spawn(entity_id, options)
  if ok and options.ttl and options.ttl > 0 then
    self:after(options.ttl, function()
      Entity.destroy(entity_id)
    end)
  end
  return ok
end

---Move this entity by (dx, dy) in 2D.
---@param self table
---@param dx number
---@param dy number
---@return boolean
function Script.move(self, dx, dy)
  return Transform.add_position(entity_id(self), dx, dy)
end

---Set this entity's 2D position.
---@param self table
---@param x number
---@param y number
---@return boolean
function Script.teleport(self, x, y)
  return Transform.set_position(entity_id(self), x, y)
end

---Move this entity by (dx, dy, dz) in 3D.
---@param self table
---@param dx number
---@param dy number
---@param dz number
---@return boolean
function Script.move3d(self, dx, dy, dz)
  return Transform3D.add_position(entity_id(self), dx, dy, dz)
end

---Set a HUD label's text.
---@param self table
---@param node_id string
---@param text string
---@return boolean
function Script.set_text(self, node_id, text)
  return Hud.set_text(node_id, text)
end

---Normalized 2D input vector for one vector2 action (diagonal-safe).
---@param self table
---@param action string
---@param player? integer
---@return number x
---@return number y
function Script.input_vector(self, action, player)
  return Input.vector(action, player)
end

return Script
