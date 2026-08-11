local Interactions = {}
Interactions.__index = Interactions

function Interactions.new(events)
  return setmetatable({ events = assert(events), candidates = {} }, Interactions)
end

function Interactions:set(id, values)
  assert(type(id) == "string")
  values = values or {}
  self.candidates[id] = { id = id, priority = values.priority or 0,
    distance = values.distance or math.huge, enabled = values.enabled ~= false,
    prompt = values.prompt, payload = values.payload }
end

function Interactions:remove(id) self.candidates[id] = nil end

function Interactions:best(max_distance)
  local result
  for _, value in pairs(self.candidates) do
    if value.enabled and value.distance <= (max_distance or math.huge) and
      (not result or value.priority > result.priority or
       value.priority == result.priority and value.distance < result.distance or
       value.priority == result.priority and value.distance == result.distance and value.id < result.id) then
      result = value
    end
  end
  return result
end

function Interactions:confirm(max_distance)
  local selected = self:best(max_distance)
  if not selected or not self.candidates[selected.id] or not selected.enabled then return false end
  self.events:emit("interaction_selected", { entity = selected.id, payload = selected.payload })
  return true, selected.id
end

return Interactions
