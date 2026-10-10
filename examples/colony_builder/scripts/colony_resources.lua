local Hud = require("demi.hud")
local Entity = require("demi.entity")
local Events = require("demi.events")

---@demi_component
---@description Prototype life support: available power runs water extraction and oxygen production.
local ColonyResources = {}

---@demi_property entity
---@label Solar Array
ColonyResources.solar_array = ""

---@demi_property entity
---@label Water Extractor
ColonyResources.water_extractor = ""

---@demi_property
---@label Solar Output (kW)
---@range 0 100
ColonyResources.power_supply = 12.0

---@demi_property
---@label Power Demand (kW)
---@range 0 100
ColonyResources.power_demand = 8.0

---@demi_property
---@label Water Production (L/s)
---@range 0 10
ColonyResources.water_production = 2.0

---@demi_property
---@label Water Capacity (L)
---@range 1 1000
ColonyResources.water_capacity = 100.0

local function operational(id)
  return id ~= "" and Entity.exists(id) and Entity.is_enabled(id)
end

function ColonyResources:available_power()
  local supply = operational(self.solar_array) and self.power_supply or 0
  for _, building in ipairs(self.additions or {}) do
    if building.kind == "solar" and operational(building.id) then supply = supply + self.power_supply end
  end
  return supply
end

function ColonyResources:demand()
  local demand = self.power_demand
  for _, building in ipairs(self.additions or {}) do
    if building.kind == "water" and operational(building.id) then demand = demand + 2 end
  end
  return demand
end

function ColonyResources:publish()
  Hud.set_text("label", string.format("Power: %+.0f kW", self:available_power() - self:demand()))
  Hud.set_text("label_copy", string.format("Water: %.0f L", self.water))
  Hud.set_text("label_copy_copy", string.format("Oxygen: %.0f%%", self.oxygen))
end

function ColonyResources:on_start()
  self.additions = {}
  self.subscription = Events.subscribe("colony.building_created", function(building)
    self.additions[#self.additions+1] = building
  end)
  self.water = math.min(25, self.water_capacity)
  self.oxygen = 25
  self.display_elapsed = 0
  self:publish()
end

function ColonyResources:on_fixed_update(dt)
  local powered = self:available_power() >= self:demand()
  local extractors = operational(self.water_extractor) and 1 or 0
  for _, building in ipairs(self.additions) do
    if building.kind == "water" and operational(building.id) then extractors = extractors + 1 end
  end
  if powered then
    self.water = math.min(self.water_capacity, self.water + extractors * self.water_production * dt)
  end
  -- One prototype oxygen generator consumes 1 L/s. Residents consume 1%/s.
  local operating = powered and self.water >= dt
  if operating then self.water = self.water - dt end
  self.oxygen = math.max(0, math.min(100, self.oxygen + (operating and 4 or -1) * dt))
  self.display_elapsed = self.display_elapsed + dt
  if self.display_elapsed >= 0.2 then
    self.display_elapsed = self.display_elapsed - 0.2
    self:publish()
  end
end

function ColonyResources:on_destroy()
  if self.subscription then Events.unsubscribe(self.subscription) end
end

return ColonyResources
