local Conditions = require("demi.data.conditions")

local Quests = {}

local function quest_state(state)
  assert(type(state) == "table", "quest state must be a table")
  state.quests = state.quests or {}
  return state.quests
end

local function objective_by_id(definition, objective_id)
  for _, objective in ipairs(definition.objectives or {}) do
    if objective.id == objective_id then return objective end
  end
end

local function validate_definition(definition)
  assert(type(definition.id) == "string" and definition.id ~= "",
    "quest definition requires a stable id")
  local ids = {}
  for _, objective in ipairs(definition.objectives or {}) do
    assert(type(objective.id) == "string" and objective.id ~= "",
      "quest objectives require stable ids")
    assert(ids[objective.id] == nil, "duplicate quest objective id: " .. objective.id)
    ids[objective.id] = true
  end
end

function Quests.start(state, definition)
  validate_definition(definition)
  local quests = quest_state(state)
  if quests[definition.id] then
    return nil, { code = "QUEST_ALREADY_STARTED", quest_id = definition.id }
  end
  quests[definition.id] = { status = "active", objectives = {} }
  return { type = "quest_started", quest_id = definition.id }
end

function Quests.progress(state, definition, objective_id, amount)
  amount = amount or 1
  validate_definition(definition)
  assert(type(amount) == "number" and amount > 0,
    "quest progress amount must be positive")
  local current = quest_state(state)[definition.id]
  if current == nil or current.status ~= "active" then
    return nil, { code = "QUEST_NOT_ACTIVE", quest_id = definition.id }
  end
  local objective = objective_by_id(definition, objective_id)
  if objective == nil then
    return nil, { code = "OBJECTIVE_NOT_FOUND", quest_id = definition.id, objective_id = objective_id }
  end
  local progress = math.min((current.objectives[objective_id] or 0) + amount, objective.target or 1)
  current.objectives[objective_id] = progress
  local events = {{
    type = "quest_objective_progressed",
    quest_id = definition.id,
    objective_id = objective_id,
    progress = progress,
    target = objective.target or 1,
  }}

  local complete = Conditions.evaluate(definition.completion_condition, state)
  if definition.completion_condition == nil then
    complete = true
    for _, candidate in ipairs(definition.objectives or {}) do
      if (current.objectives[candidate.id] or 0) < (candidate.target or 1) then
        complete = false
        break
      end
    end
  end
  if complete then
    current.status = "completed"
    events[#events + 1] = { type = "quest_completed", quest_id = definition.id }
  end
  return events
end

return Quests
