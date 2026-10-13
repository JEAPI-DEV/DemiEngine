local Entity = require("demi.entity")
local Hud = require("demi.hud")
local Prefab = require("demi.prefab")
local Events = require("demi.gameplay.events")
local Inventory = require("demi.gameplay.inventory")

local Logistics = {}
Logistics.__index = Logistics

function Logistics.new(config)
  local bus = Events.new()
  local self = setmetatable(
    { config = config, bus = bus, orders = {}, piles = {}, spent = 0, next_pile = 1 },
    Logistics
  )
  self.depot = Inventory.new(bus, 2)
  self.cargo = Inventory.new(bus, 1)
  self.depot:add("metal", config.starting_metal)
  self.depot:add("meal", config.starting_meals)
  self.initial_metal = self.depot:count("metal")
  return self
end

function Logistics:reserved()
  local total = 0
  for _, job in pairs(self.orders) do
    total = total + job.remaining
  end
  return total
end

function Logistics:available()
  return self.depot:count("metal") - self:reserved()
end

function Logistics:reserve(job)
  if self:available() < job.cost then
    return false
  end
  job.remaining = job.cost
  job.materials = Inventory.new(self.bus, 1)
  self.orders[job.id] = job
  return true
end

function Logistics:pickup(job)
  if not Entity.is_enabled(self.config.depot) then
    return 0
  end
  local moved = self.depot:transfer_to(
    self.cargo,
    "metal",
    math.min(job.remaining, self.config.carry_capacity),
    self.config.carry_capacity
  )
  job.remaining = job.remaining - moved
  if moved > 0 then
    self.cargo_job = job
  end
  return moved
end

function Logistics:deliver(job)
  local moved = self.cargo:transfer_to(job.materials, "metal", self.cargo:count("metal"))
  self.cargo_job = nil
  return moved
end

function Logistics:return_cargo()
  if not Entity.is_enabled(self.config.depot) then
    return 0
  end
  local moved = self.cargo:transfer_to(self.depot, "metal", self.cargo:count("metal"))
  self.cargo_job = nil
  return moved
end

function Logistics:cancel(job)
  local delivered = job.materials:count("metal")
  if delivered > 0 then
    local id = "recovery_" .. self.next_pile
    local y = job.ground_y or (job.site and (job.site.ground_y or job.site.y)) or 0
    if
      not Prefab.instantiate(
        "prefab://utilities/materials",
        { id = id, position = { job.x, y, job.z } }
      )
    then
      return false, "Could not preserve delivered materials; plan was not cancelled"
    end
    local pile = { id = id, x = job.x, y = y, z = job.z, inventory = Inventory.new(self.bus, 1) }
    job.materials:transfer_to(pile.inventory, "metal", delivered)
    self.piles[#self.piles + 1] = pile
    self.next_pile = self.next_pile + 1
  end
  -- Uncollected reservations are immediately available. Physical cargo/site
  -- stock remains outside the depot until a carrier returns it.
  self.orders[job.id] = nil
  job.remaining = 0
  if self.cargo_job == job then
    self.cargo_job = nil
  end
  return true
end

function Logistics:recover(pile)
  local moved = pile.inventory:transfer_to(
    self.cargo,
    "metal",
    self.config.carry_capacity,
    self.config.carry_capacity
  )
  self.cargo_job = nil
  if pile.inventory:count("metal") == 0 then
    Prefab.release(pile.id)
    for i, item in ipairs(self.piles) do
      if item == pile then
        table.remove(self.piles, i)
        break
      end
    end
  end
  return moved
end

function Logistics:consume(job)
  assert(job.materials:count("metal") >= job.cost, "Construction needs delivered materials")
  self.spent = self.spent + job.materials:remove("metal", job.cost)
  self.orders[job.id] = nil
end

function Logistics:summary()
  local delivered, recoverable = 0, 0
  for _, job in pairs(self.orders) do
    delivered = delivered + job.materials:count("metal")
  end
  for _, pile in ipairs(self.piles) do
    recoverable = recoverable + pile.inventory:count("metal")
  end
  local stock, carried = self.depot:count("metal"), self.cargo:count("metal")
  assert(
    math.abs(stock + carried + delivered + recoverable + self.spent - self.initial_metal) < 0.000001,
    "Metal conservation failed"
  )
  assert(self:reserved() <= stock, "Reservations exceed depot stock")
  return stock, self:reserved(), carried, delivered, recoverable
end

function Logistics:publish()
  local stock, reserved, carried, delivered, recovery = self:summary()
  Hud.set_text("metal", string.format("Metal: %.0f", stock - reserved))
  Hud.set_text("meals", string.format("Meals: %.0f", self.depot:count("meal")))
  Hud.set_text(
    "logistics_status",
    string.format(
      "Depot: %.0f   Reserved: %.0f   Carrying: %.0f   On sites: %.0f   Recoverable: %.0f",
      stock,
      reserved,
      carried,
      delivered,
      recovery
    )
  )
  if carried ~= self.last_carry then
    Entity.set_enabled(self.config.cargo, carried > 0)
    self.last_carry = carried
  end
  self.bus:flush()
end

return Logistics
