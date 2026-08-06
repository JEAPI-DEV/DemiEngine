local Flags = require("demi.data.flags")

local Conditions = {}

local function compare(operator, actual, expected)
  if operator == "equals" then return actual == expected end
  if operator == "not_equals" then return actual ~= expected end
  local comparable = type(actual) == type(expected)
    and (type(actual) == "number" or type(actual) == "string")
  if not comparable then return false end
  if operator == "greater" then return actual > expected end
  if operator == "greater_or_equal" then return actual >= expected end
  if operator == "less" then return actual < expected end
  if operator == "less_or_equal" then return actual <= expected end
  error("unknown condition operator: " .. tostring(operator))
end

function Conditions.evaluate(condition, state)
  if condition == nil then return true end
  assert(type(condition) == "table", "condition must be a table")

  if condition.all then
    for _, child in ipairs(condition.all) do
      if not Conditions.evaluate(child, state) then return false end
    end
    return true
  end
  if condition.any then
    for _, child in ipairs(condition.any) do
      if Conditions.evaluate(child, state) then return true end
    end
    return false
  end
  if condition["not"] then
    return not Conditions.evaluate(condition["not"], state)
  end

  assert(type(condition.id) == "string", "leaf condition requires an id")
  local actual = Flags.get(state, condition.id)
  if condition.operator == nil then return actual == true end
  return compare(condition.operator, actual, condition.value)
end

return Conditions
