local Inventory = {}
Inventory.__index = Inventory

function Inventory.new(events, capacity)
  return setmetatable({ events = assert(events), capacity = capacity or 20, stacks = {}, equipment = {} }, Inventory)
end

function Inventory:add(item, count, maximum)
  count, maximum = count or 1, maximum or math.huge
  if count <= 0 then return 0 end
  local current = self.stacks[item] or 0
  local added = math.min(count, maximum - current)
  if current == 0 and added > 0 then
    local unique = 0; for _ in pairs(self.stacks) do unique = unique + 1 end
    if unique >= self.capacity then return 0 end
  end
  if added > 0 then self.stacks[item] = current + added; self.events:emit("inventory_changed", { item = item, count = self.stacks[item] }) end
  return added
end

function Inventory:remove(item, count)
  local current = self.stacks[item] or 0; local removed = math.min(current, math.max(0, count or 1))
  local left = current - removed; self.stacks[item] = left > 0 and left or nil
  if removed > 0 then self.events:emit("inventory_changed", { item = item, count = left }) end
  return removed
end

function Inventory:equip(slot, item)
  if item and not self.stacks[item] then return false end
  self.equipment[slot] = item; self.events:emit("equipment_changed", { slot = slot, item = item }); return true
end

function Inventory:save() return { stacks = self.stacks, equipment = self.equipment, capacity = self.capacity } end
function Inventory:load(value)
  if type(value) ~= "table" or type(value.stacks) ~= "table" then return false end
  self.stacks, self.equipment, self.capacity = value.stacks, value.equipment or {}, value.capacity or self.capacity
  return true
end

return Inventory
