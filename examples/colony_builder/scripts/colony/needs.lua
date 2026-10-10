local Hud = require("demi.hud")
local Needs = {}
Needs.__index = Needs
function Needs.new(config)
  return setmetatable({stamina=config.starting_stamina, nutrition=config.starting_nutrition,
    oxygen=config.starting_suit_oxygen, forced=false}, Needs)
end
function Needs:update(dt,working,resting)
  self.nutrition = math.max(0,self.nutrition-0.15*dt)
  if not resting then
    self.stamina = math.max(0,self.stamina-(working and 0.4 or 0.18)*dt)
    self.oxygen = math.max(0,self.oxygen-0.55*dt)
  end
end
function Needs:urgent()
  return self.forced or self.stamina<=25 or self.nutrition<=30 or self.oxygen<=30
end
function Needs:recover(dt,resources,logistics)
  local ok,reason = resources:habitable()
  if not ok then return false,reason end
  self.stamina = math.min(100,self.stamina+8*dt)
  self.oxygen = math.min(100,self.oxygen+12*dt)
  resources.water = math.max(0,resources.water-0.1*dt)
  if self.nutrition<85 and logistics.depot:remove("meal",1)>0 then
    self.nutrition = math.min(100,self.nutrition+60)
  end
  if self.nutrition<70 then return false,"No meals · waiting at the habitat" end
  local ready = self.stamina>=90 and self.nutrition>=70 and self.oxygen>=95
  if ready then self.forced=false end
  return ready,"Resting and refilling suit oxygen"
end
function Needs:publish()
  Hud.set_text("needs_status",string.format("Mara   Stamina: %.0f%%   Nutrition: %.0f%%   Suit O2: %.0f%%",
    self.stamina,self.nutrition,self.oxygen))
end
return Needs
