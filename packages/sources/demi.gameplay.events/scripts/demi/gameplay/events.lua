local Events = {}
Events.__index = Events

function Events.new()
  return setmetatable({ listeners = {}, queue = {}, next_order = 1 }, Events)
end

function Events:on(name, callback, priority)
  assert(type(name) == "string" and type(callback) == "function")
  local entry = { callback = callback, priority = priority or 0, order = self.next_order, active = true }
  self.next_order = self.next_order + 1
  self.listeners[name] = self.listeners[name] or {}
  table.insert(self.listeners[name], entry)
  return function() entry.active = false end
end

function Events:emit(name, payload)
  table.insert(self.queue, { name = name, payload = payload })
end

function Events:flush()
  while #self.queue > 0 do
    local event = table.remove(self.queue, 1)
    local listeners = self.listeners[event.name] or {}
    table.sort(listeners, function(a, b)
      return a.priority == b.priority and a.order < b.order or a.priority > b.priority
    end)
    for index = 1, #listeners do
      if listeners[index].active then listeners[index].callback(event.payload) end
    end
    local retained = {}
    for index = 1, #listeners do
      if listeners[index].active then table.insert(retained, listeners[index]) end
    end
    self.listeners[event.name] = retained
  end
end

return Events
