local Weapon = {}
Weapon.__index = Weapon

function Weapon.new(values)
  values = values or {}
  return setmetatable({ cooldown = values.cooldown or 0, remaining = 0,
    ammo = values.ammo, magazine = values.magazine, reload = 0 }, Weapon)
end

function Weapon:update(dt) self.remaining = math.max(0, self.remaining - dt) end
function Weapon:can_fire() return self.remaining == 0 and (self.ammo == nil or self.ammo > 0) end
function Weapon:fire()
  if not self:can_fire() then return false end
  self.remaining = self.cooldown
  if self.ammo then self.ammo = self.ammo - 1 end
  return true
end

return Weapon
