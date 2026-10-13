-- Named, bounded needs. Higher values mean the need is better satisfied.
-- Games own recovery activities, resource costs, HUD and scheduling decisions.
local Needs = {}
Needs.__index = Needs

local function finite(value)
  return type(value) == "number" and value == value and math.abs(value) < math.huge
end

function Needs.new(definitions)
  assert(type(definitions) == "table", "need definitions must be a table")
  local self = setmetatable({ entries = {} }, Needs)

  for name, definition in pairs(definitions) do
    assert(type(name) == "string" and name ~= "", "need names must be nonempty strings")
    assert(type(definition) == "table", "each need requires a definition")
    local maximum = definition.maximum or 100
    local value = definition.value or maximum
    local low = definition.low or maximum * 0.25
    local recovered = definition.recovered or maximum * 0.9

    assert(finite(maximum) and maximum > 0, "maximum must be positive and finite")
    assert(finite(value), "initial value must be finite")
    assert(finite(low) and low >= 0 and low <= maximum, "low threshold must be within bounds")
    assert(
      finite(recovered) and recovered > low and recovered <= maximum,
      "recovered threshold must exceed low and be within bounds"
    )

    self.entries[name] = {
      value = math.max(0, math.min(maximum, value)),
      maximum = maximum,
      low = low,
      recovered = recovered,
    }
  end
  return self
end

function Needs:entry(name)
  return assert(self.entries[name], "unknown need: " .. tostring(name))
end

function Needs:get(name)
  return self:entry(name).value
end

function Needs:change(name, amount)
  assert(finite(amount), "need change must be finite")
  local entry = self:entry(name)
  local previous = entry.value
  entry.value = math.max(0, math.min(entry.maximum, previous + amount))
  return entry.value - previous
end

function Needs:update(dt, rates)
  assert(finite(dt) and dt >= 0, "dt must be nonnegative and finite")
  assert(type(rates) == "table", "rates must be a table")

  -- Validate the whole request before mutating any meter.
  for name, rate in pairs(rates) do
    self:entry(name)
    assert(finite(rate) and finite(rate * dt), "need rates must be finite")
  end
  for name, rate in pairs(rates) do
    self:change(name, rate * dt)
  end
end

function Needs:urgent()
  for _, entry in pairs(self.entries) do
    if entry.value <= entry.low then
      return true
    end
  end
  return false
end

function Needs:recovered()
  for _, entry in pairs(self.entries) do
    if entry.value < entry.recovered then
      return false
    end
  end
  return true
end

return Needs
