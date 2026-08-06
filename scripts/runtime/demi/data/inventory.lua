local Inventory = {}

local function counts(state)
  assert(type(state) == "table", "inventory state must be a table")
  state.inventory = state.inventory or {}
  return state.inventory
end

function Inventory.count(state, item_id)
  return counts(state)[item_id] or 0
end

function Inventory.add(state, item_id, amount)
  amount = amount or 1
  assert(type(item_id) == "string" and item_id ~= "", "item id must be a non-empty string")
  assert(type(amount) == "number" and amount > 0 and amount % 1 == 0,
    "inventory amount must be a positive integer")
  local items = counts(state)
  items[item_id] = (items[item_id] or 0) + amount
  return { type = "item_added", item_id = item_id, amount = amount, count = items[item_id] }
end

function Inventory.remove(state, item_id, amount)
  amount = amount or 1
  assert(type(item_id) == "string" and item_id ~= "", "item id must be a non-empty string")
  assert(type(amount) == "number" and amount > 0 and amount % 1 == 0,
    "inventory amount must be a positive integer")
  local items = counts(state)
  local current = items[item_id] or 0
  if current < amount then
    return nil, { code = "INSUFFICIENT_ITEMS", item_id = item_id, requested = amount, count = current }
  end
  local remaining = current - amount
  items[item_id] = remaining > 0 and remaining or nil
  return { type = "item_removed", item_id = item_id, amount = amount, count = remaining }
end

return Inventory
