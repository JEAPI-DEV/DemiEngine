-- Weapon timing/flight policy only. Collision queries and fracture remain native.
local Weapons = {}
Weapons.__index = Weapons

local defaults = {
  hammer_reach = 2.6, hammer_radius = 0.15, hammer_energy = 18000, hammer_impulse = 220,
  windup = 0.16, swing = 0.12, recovery = 0.28,
  rocket_speed = 8, rocket_radius = 0.15, rocket_life = 5, rocket_cooldown = 0.7,
  blast_radius = 1.5, blast_energy = 240000, blast_impulse = 1800,
  ammo = 8, max_rockets = 8,
}

local function finite(value) return type(value) == "number" and value == value and math.abs(value) < math.huge end

local function vector(value)
  assert(type(value)=="table" and #value==3 and finite(value[1]) and finite(value[2]) and finite(value[3]), "aim requires finite [x,y,z]")
  assert(math.abs(value[1])<=1e6 and math.abs(value[2])<=1e6 and math.abs(value[3])<=1e6, "aim coordinates exceed supported limits")
end

function Weapons.new(services, options)
  assert(type(services)=="table", "provide weapon services")
  for _,name in ipairs({"cast","impact","spawn","move","release"}) do
    assert(type(services[name])=="function", "weapon service must be callable: "..name)
  end
  assert(options and type(options.id_prefix)=="string" and options.id_prefix~="", "provide a unique weapon id_prefix")
  for name in pairs(options) do
    assert(name=="id_prefix" or defaults[name]~=nil, "unknown weapon setting: "..tostring(name))
  end
  local settings = {}
  for name, fallback in pairs(defaults) do
    local value = options and options[name]
    if value == nil then value = fallback end
    assert(finite(value) and value >= 0, "invalid weapon setting: " .. name)
    settings[name] = value
  end
  assert(settings.windup > 0 and settings.swing > 0 and settings.recovery > 0, "attack phases must be positive")
  assert(finite(settings.windup + settings.swing + settings.recovery), "attack duration overflow")
  assert(settings.rocket_speed > 0 and settings.rocket_life > 0 and settings.hammer_reach > 0, "invalid weapon range/lifetime")
  assert(settings.rocket_speed <= 1000 and settings.rocket_life <= 60 and settings.hammer_reach <= 1000,
    "weapon range/speed/lifetime exceeds supported limits")
  for _,name in ipairs({"hammer_radius","rocket_radius","blast_radius"}) do
    assert(settings[name] >= 0.001 and settings[name] <= 1000, "invalid radius: "..name)
  end
  assert(settings.hammer_energy <= 1e12 and settings.blast_energy <= 1e12 and
    settings.hammer_impulse <= 1e9 and settings.blast_impulse <= 1e9, "impact budget exceeds native limits")
  assert(settings.ammo % 1 == 0 and settings.ammo <= 1000000 and settings.max_rockets % 1 == 0 and settings.max_rockets >= 1 and settings.max_rockets <= 64, "invalid ammunition/projectile limit")
  return setmetatable({ services = services, settings = settings,
    vector = services.vector or require("demi.math.vector3"),
    prefix = assert(options and options.id_prefix, "provide a unique weapon id_prefix"),
    ammo = settings.ammo, cooldown = 0, clock = nil, contacted = false,
    rockets = {}, serial = 0, events = {}, shots = 0, detonations = 0, contacts = 0 }, Weapons)
end

function Weapons:emit(kind, values)
  values = values or {}; values.kind = kind
  if #self.events == 32 then table.remove(self.events, 1) end
  self.events[#self.events + 1] = values
end

function Weapons:drain_events()
  local events = self.events; self.events = {}; return events
end

function Weapons:aim(origin, direction)
  vector(origin); vector(direction)
  assert(self.vector.length(direction) > 0.000001, "aim direction must be nonzero")
  return {origin[1], origin[2], origin[3]}, self.vector.normalized(direction)
end

function Weapons:hammer()
  if self.clock then return false, "Hammer recovering" end
  self.clock, self.contacted = 0, false
  self:emit("hammer_started")
  return true
end

function Weapons:hammer_phase()
  if not self.clock then return "idle", 0 end
  local s, t = self.settings, self.clock
  if t < s.windup then return "windup", t / s.windup end
  if t < s.windup + s.swing then return "swing", (t - s.windup) / s.swing end
  return "recovery", math.min(1, (t - s.windup - s.swing) / s.recovery)
end

function Weapons:detonate(hit)
  local v, s = self.vector, self.settings
  self.detonations = self.detonations + 1
  local position = v.add(hit.point, v.scale(hit.normal or {0,0,0}, 0.1))
  local ok, issue, affected, receipt = self.services.impact({ position = position,
    radius = s.blast_radius, energy = s.blast_energy, impulse = s.blast_impulse }, hit.entity_id)
  self:emit("explosion", {position = position, accepted = ok, error = issue,
    affected = affected or 0, receipt = receipt})
end

function Weapons:fire(origin, direction)
  if self.cooldown > 0 then return false, "Rocket cooling down" end
  if self.ammo == 0 then return false, "Out of rockets" end
  if #self.rockets >= self.settings.max_rockets then return false, "Projectile limit reached" end
  origin, direction = self:aim(origin, direction)
  local inside = self.services.cast(origin, direction, 0, self.settings.rocket_radius)
  local id = self.prefix .. tostring(self.serial + 1)
  if not inside and not self.services.spawn(id, origin, direction) then return false, "Rocket visual spawn failed" end
  self.serial, self.shots = self.serial + 1, self.shots + 1
  self.ammo, self.cooldown = self.ammo - 1, self.settings.rocket_cooldown
  self:emit("rocket_fired", {id = id})
  if inside then self:detonate(inside)
  else self.rockets[#self.rockets + 1] = {id = id, position = origin, direction = direction, life = self.settings.rocket_life} end
  return true
end

function Weapons:update(dt, origin, direction)
  assert(finite(dt) and dt >= 0, "weapon dt must be finite and non-negative")
  origin, direction = self:aim(origin, direction)
  self.cooldown = math.max(0, self.cooldown - dt)
  if self.clock then
    self.clock = self.clock + dt
    local s = self.settings
    if not self.contacted and self.clock >= s.windup + s.swing then
      self.contacted = true
      local hit = self.services.cast(origin, direction, s.hammer_reach, s.hammer_radius)
      if hit then
        self.contacts = self.contacts + 1
        local ok, issue, affected, receipt = self.services.impact({ entity = hit.entity_id, position = hit.point,
          direction = direction, radius = s.hammer_radius, energy = s.hammer_energy, impulse = s.hammer_impulse }, hit.entity_id)
        self:emit("hammer_contact", {accepted = ok, error = issue, affected = affected or 0, receipt = receipt})
      else self:emit("hammer_miss") end
    end
    if self.clock >= s.windup + s.swing + s.recovery then self.clock = nil end
  end
  for index = #self.rockets, 1, -1 do
    local rocket, s = self.rockets[index], self.settings
    local elapsed = math.min(dt, rocket.life)
    local distance = elapsed * s.rocket_speed
    local hit = self.services.cast(rocket.position, rocket.direction, distance, s.rocket_radius)
    rocket.life = rocket.life - elapsed
    if hit or rocket.life <= 0 then
      if hit then self:detonate(hit) else self:emit("rocket_expired", {id = rocket.id}) end
      self.services.release(rocket.id)
      table.remove(self.rockets, index)
    else
      rocket.position = self.vector.add(rocket.position, self.vector.scale(rocket.direction, distance))
      self.services.move(rocket.id, rocket.position, rocket.direction)
    end
  end
end

function Weapons:reload()
  if #self.rockets > 0 then return false, "Wait for active rockets" end
  self.ammo = self.settings.ammo
  return true
end

function Weapons:dispose()
  for _, rocket in ipairs(self.rockets) do self.services.release(rocket.id) end
  self.rockets, self.clock = {}, nil
end

return Weapons
