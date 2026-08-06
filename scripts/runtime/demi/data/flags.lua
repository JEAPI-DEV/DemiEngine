local Flags = {}

local function values(state)
  assert(type(state) == "table", "flags state must be a table")
  state.flags = state.flags or {}
  return state.flags
end

function Flags.get(state, id, default)
  local value = values(state)[id]
  if value == nil then return default end
  return value
end

function Flags.set(state, id, value)
  assert(type(id) == "string" and id ~= "", "flag id must be a non-empty string")
  values(state)[id] = value
  return { type = "flag_changed", id = id, value = value }
end

function Flags.toggle(state, id)
  return Flags.set(state, id, not Flags.get(state, id, false))
end

return Flags
