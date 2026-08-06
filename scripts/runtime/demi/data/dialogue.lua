local Conditions = require("demi.data.conditions")

local Dialogue = {}

local function nodes_by_id(definition)
  local nodes = {}
  for _, node in ipairs(definition.nodes or {}) do
    assert(type(node.id) == "string", "dialogue nodes require stable ids")
    assert(nodes[node.id] == nil, "duplicate dialogue node id: " .. node.id)
    nodes[node.id] = node
  end
  return nodes
end

function Dialogue.start(definition, node_id)
  assert(type(definition.id) == "string", "dialogue definition requires a stable id")
  local current = node_id or definition.start
  if nodes_by_id(definition)[current] == nil then
    return nil, { code = "DIALOGUE_NODE_NOT_FOUND", dialogue_id = definition.id, node_id = current }
  end
  return { dialogue_id = definition.id, node_id = current },
    { type = "dialogue_started", dialogue_id = definition.id, node_id = current }
end

function Dialogue.current(definition, session, state)
  if session.completed then
    return nil, { code = "DIALOGUE_COMPLETE", dialogue_id = definition.id }
  end
  local node = nodes_by_id(definition)[session.node_id]
  if node == nil then
    return nil, { code = "DIALOGUE_SAVED_NODE_MISSING", dialogue_id = definition.id, node_id = session.node_id }
  end
  local choices = {}
  for _, choice in ipairs(node.choices or {}) do
    if Conditions.evaluate(choice.condition, state) then choices[#choices + 1] = choice end
  end
  return { id = node.id, text = node.text, speaker = node.speaker, choices = choices }
end

function Dialogue.choose(definition, session, choice_id, state)
  local node, error = Dialogue.current(definition, session, state)
  if node == nil then return nil, error end
  for _, choice in ipairs(node.choices) do
    if choice.id == choice_id then
      local previous = session.node_id
      if choice.next ~= nil and nodes_by_id(definition)[choice.next] == nil then
        return nil, {
          code = "DIALOGUE_NEXT_NODE_NOT_FOUND",
          dialogue_id = definition.id,
          choice_id = choice_id,
          node_id = choice.next,
        }
      end
      session.node_id = choice.next
      session.completed = choice.next == nil
      return {
        type = "dialogue_choice_selected",
        dialogue_id = definition.id,
        node_id = previous,
        choice_id = choice_id,
        next_node_id = choice.next,
      }
    end
  end
  return nil, { code = "DIALOGUE_CHOICE_NOT_AVAILABLE", dialogue_id = definition.id, choice_id = choice_id }
end

return Dialogue
