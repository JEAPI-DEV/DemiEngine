---@class DemiInputBuffer
---@field max_age number
---@field entries table[]
local Buffer = {}
Buffer.__index = Buffer

function Buffer.new(max_age)
  return setmetatable({ max_age = max_age or 0.2, entries = {} }, Buffer)
end

function Buffer:push(action, now)
  table.insert(self.entries, { action = action, time = now })
  self:prune(now)
end

function Buffer:prune(now)
  while self.entries[1] and now - self.entries[1].time > self.max_age do
    table.remove(self.entries, 1)
  end
end

function Buffer:consume(action, now)
  self:prune(now)
  for index, entry in ipairs(self.entries) do
    if entry.action == action then
      table.remove(self.entries, index)
      return true
    end
  end
  return false
end

function Buffer:clear()
  self.entries = {}
end

return Buffer
