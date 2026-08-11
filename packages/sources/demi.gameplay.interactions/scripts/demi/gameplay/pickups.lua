local Pickups = {}
Pickups.__index = Pickups

function Pickups.new(events)
  return setmetatable({ events = assert(events), entries = {} }, Pickups)
end

function Pickups:add(id, values)
  assert(type(id) == "string")
  values = values or {}
  self.entries[id] = {
    id = id,
    item = values.item,
    count = values.count or 1,
    enabled = values.enabled ~= false,
    payload = values.payload,
  }
end

function Pickups:remove(id)
  self.entries[id] = nil
end

function Pickups:collect(id, collector)
  local pickup = self.entries[id]
  if not pickup or not pickup.enabled then return false end
  pickup.enabled = false
  self.entries[id] = nil
  self.events:emit("pickup_collected", {
    pickup = id,
    collector = collector,
    item = pickup.item,
    count = pickup.count,
    payload = pickup.payload,
  })
  return true
end

return Pickups
