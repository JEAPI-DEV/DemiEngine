local Hud = require("demi.hud")
local Needs = require("demi.gameplay.needs")

-- This adapter owns colony rules. The package only manages bounded meters.
local ColonyNeeds = {}
ColonyNeeds.__index = ColonyNeeds

function ColonyNeeds.new(config)
  return setmetatable({
    meters = Needs.new({
      stamina = { value = config.starting_stamina, low = 25, recovered = 90 },
      nutrition = { value = config.starting_nutrition, low = 30, recovered = 70 },
      oxygen = { value = config.starting_suit_oxygen, low = 30, recovered = 95 },
    }),
    forced = false,
  }, ColonyNeeds)
end

function ColonyNeeds:update(dt, working, resting)
  local rates = { nutrition = -0.15 }
  if not resting then
    rates.stamina = working and -0.4 or -0.18
    rates.oxygen = -0.55
  end
  self.meters:update(dt, rates)
end

function ColonyNeeds:urgent()
  return self.forced or self.meters:urgent()
end

function ColonyNeeds:recover(dt, resources, logistics)
  local habitable, reason = resources:habitable()
  if not habitable then
    return false, reason
  end

  self.meters:update(dt, { stamina = 8, oxygen = 12 })
  resources.water = math.max(0, resources.water - 0.1 * dt)

  -- Food belongs to the depot inventory, not to the needs package. Only restore
  -- nutrition after a meal was actually removed from stock.
  if self.meters:get("nutrition") < 85 and logistics.depot:remove("meal", 1) > 0 then
    self.meters:change("nutrition", 60)
  end
  if self.meters:get("nutrition") < 70 then
    return false, "No meals · waiting at the habitat"
  end

  local ready = self.meters:recovered()
  if ready then
    self.forced = false
  end
  return ready, "Resting and refilling suit oxygen"
end

function ColonyNeeds:publish()
  Hud.set_text(
    "needs_status",
    string.format(
      "Mara   Stamina: %.0f%%   Nutrition: %.0f%%   Suit O2: %.0f%%",
      self.meters:get("stamina"),
      self.meters:get("nutrition"),
      self.meters:get("oxygen")
    )
  )
end

return ColonyNeeds
