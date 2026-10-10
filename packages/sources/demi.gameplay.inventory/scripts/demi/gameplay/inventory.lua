local function valid_amount(value)
  return type(value)=="number" and value==value and value>=0 and value<math.huge
end

local Inventory = {}
Inventory.__index = Inventory

function Inventory.new(events, capacity)
  return setmetatable({ events = assert(events), capacity = capacity or 20, stacks = {}, equipment = {} }, Inventory)
end

function Inventory:add(item, count, maximum)
  count, maximum = count or 1, maximum or math.huge
  if type(item)~="string" or item=="" or not valid_amount(count) or count==0 or
      type(maximum)~="number" or maximum~=maximum or maximum<0 then return 0 end
  local current = self.stacks[item] or 0
  local added = math.max(0, math.min(count, maximum - current))
  if current == 0 and added > 0 then
    local unique = 0; for _ in pairs(self.stacks) do unique = unique + 1 end
    if unique >= self.capacity then return 0 end
  end
  if added > 0 then self.stacks[item] = current + added; self.events:emit("inventory_changed", { item = item, count = self.stacks[item] }) end
  return added
end

function Inventory:remove(item, count)
  if count~=nil and not valid_amount(count) then return 0 end
  local current = self.stacks[item] or 0; local removed = math.min(current, math.max(0, count or 1))
  local left = current - removed; self.stacks[item] = left > 0 and left or nil
  if removed > 0 then self.events:emit("inventory_changed", { item = item, count = left }) end
  return removed
end

function Inventory:count(item) return self.stacks[item] or 0 end

-- Transfer only what the destination accepts. Both inventories are changed before
-- queued events can be flushed; rejected transfers never remove source items.
function Inventory:transfer_to(destination, item, count, maximum)
  if getmetatable(destination)~=Inventory or destination==self or not valid_amount(count) then return 0 end
  local moved=destination:add(item,math.min(self:count(item),count),maximum)
  if moved>0 then self:remove(item,moved) end
  return moved
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
