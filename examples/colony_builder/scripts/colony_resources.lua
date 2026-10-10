local Hud = require("demi.hud")

local ColonyResources = {}

function ColonyResources.new(config,network)
  local self=setmetatable({network=network},{__index=ColonyResources})
  for _,key in ipairs({"power_supply","power_demand","water_production","water_capacity"}) do self[key]=config[key] end
  self.water,self.oxygen,self.display_elapsed=math.min(25,self.water_capacity),25,0
  return self
end
function ColonyResources:available_power()
  local solar=self.network:counts()
  return solar*self.power_supply
end
function ColonyResources:demand()
  if not self.network.connected[self.network.root] then return 0 end
  local _,water=self.network:counts()
  return self.power_demand+water*2
end

function ColonyResources:habitable()
  if not self.network.connected[self.network.root] or self:available_power()<self:demand() then
    return false,"Habitat has no power · waiting for life support"
  end
  if self.oxygen<35 then return false,"Habitat oxygen too low · waiting for life support" end
  if self.water<0.5 then return false,"Habitat has no water · waiting for life support" end
  return true
end

function ColonyResources:publish()
  Hud.set_text("label", string.format("Power: %+.0f kW", self:available_power() - self:demand()))
  Hud.set_text("label_copy", string.format("Water: %.0f L", self.water))
  Hud.set_text("label_copy_copy", string.format("Oxygen: %.0f%%", self.oxygen))
end

function ColonyResources:update(dt)
  local powered = self.network.connected[self.network.root] and self:available_power() >= self:demand()
  local _,extractors=self.network:counts()
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

return ColonyResources
