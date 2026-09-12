local Melee = {}
Melee.__index = Melee

function Melee.new(definition)
  assert(type(definition) == "table", "attack definition required")
  assert(definition.windup >= 0 and definition.active > 0 and
    definition.recovery >= 0, "invalid attack phases")
  assert(definition.reach > 0 and definition.damage >= 0, "invalid attack damage/reach")
  return setmetatable({ definition = definition, elapsed = 0, running = false,
    victims = {} }, Melee)
end

function Melee:start()
  if self.running then return false end
  self.elapsed, self.running, self.victims = 0, true, {}
  return true
end

function Melee:phase()
  if not self.running then return "idle" end
  local d = self.definition
  if self.elapsed < d.windup then return "windup" end
  if self.elapsed < d.windup + d.active then return "active" end
  return "recovery"
end

function Melee:update(dt)
  assert(dt >= 0, "negative timestep")
  if not self.running then return end
  self.elapsed = self.elapsed + dt
  local d = self.definition
  if self.elapsed >= d.windup + d.active + d.recovery then self.running = false end
end

-- Call only after all combatants have processed this tick's input/defence.
-- An avoided swing is consumed too: leaving i-frames cannot re-hit this swing.
function Melee:hit(id, distance, facing_dot, invulnerable)
  local d = self.definition
  if self:phase() ~= "active" or self.victims[id] or distance > d.reach or
    facing_dot < (d.minimum_dot or 0) then return 0 end
  self.victims[id] = true
  return invulnerable and 0 or d.damage
end

return Melee

