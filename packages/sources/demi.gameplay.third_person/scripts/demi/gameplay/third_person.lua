local Character = {}
Character.__index = Character

function Character.new(options)
  options = options or {}
  local self = setmetatable({ speed = options.speed or 4,
    roll_speed = options.roll_speed or 7, roll_duration = options.roll_duration or 0.6,
    iframe_duration = options.iframe_duration or 0.35,
    roll_cost = options.roll_cost or 25, stamina_max = options.stamina or 100,
    regeneration = options.regeneration or 20, roll_left = 0,
    vx = 0, vz = 0, facing_x = 0, facing_z = -1 }, Character)
  assert(self.roll_duration > 0 and self.iframe_duration <= self.roll_duration and
    self.iframe_duration >= 0 and self.roll_cost >= 0, "invalid roll settings")
  self.stamina = self.stamina_max
  return self
end

function Character:invulnerable()
  return self.roll_left > 0 and
    self.roll_duration - self.roll_left < self.iframe_duration
end

function Character:update(input, forward_x, forward_z, dt, movement_locked)
  assert(dt >= 0, "negative timestep")
  self.roll_left = math.max(0, self.roll_left - dt)
  local x, z = input.x or 0, input.y or 0
  local magnitude = math.sqrt(x*x + z*z)
  if magnitude > 1 then x, z = x/magnitude, z/magnitude end
  local dx = -forward_z*x + forward_x*z
  local dz = forward_x*x + forward_z*z
  if input.roll and self.roll_left == 0 and not movement_locked and
    self.stamina >= self.roll_cost then
    local length = math.sqrt(dx*dx + dz*dz)
    self.roll_x = length > 0.001 and dx/length or self.facing_x
    self.roll_z = length > 0.001 and dz/length or self.facing_z
    self.roll_left = self.roll_duration
    self.stamina = self.stamina - self.roll_cost
  end
  if self.roll_left > 0 then
    self.vx, self.vz = self.roll_x*self.roll_speed, self.roll_z*self.roll_speed
    self.facing_x, self.facing_z = self.roll_x, self.roll_z
    return "roll"
  end
  if movement_locked then self.vx, self.vz = 0, 0; return "attack" end
  self.stamina = math.min(self.stamina_max, self.stamina + self.regeneration*dt)
  self.vx, self.vz = dx*self.speed, dz*self.speed
  local length = math.sqrt(dx*dx + dz*dz)
  if length > 0.001 then
    self.facing_x, self.facing_z = dx/length, dz/length
    return "run"
  end
  return "idle"
end

return Character

