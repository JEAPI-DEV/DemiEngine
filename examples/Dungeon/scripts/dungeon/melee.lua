local Body = require("dungeon.body")
local Melee = {}

-- Radians, world units and seconds. The same profiles drive hits and visuals.
Melee.profiles = {
  unarmed = { range = 0.8, arc = 1.2, windup = 0.12, active = 0.1, recovery = 0.25 },
  short_sword = { range = 1.55, arc = 1.65, windup = 0.13, active = 0.12, recovery = 0.23 },
  long_sword = { range = 2.65, arc = 2.6, windup = 0.55, active = 0.25, recovery = 0.65 },
  guard_sword = { range = 2.75, arc = 2.7, windup = 0.85, active = 0.28, recovery = 1.05 },
  duelist_sword = { range = 1.55, arc = 1.5, windup = 0.35, active = 0.14, recovery = 0.5 },
  bite = { range = 1.1, arc = 1.2, windup = 0.4, active = 0.22, recovery = 0.85 },
  crown_sword = { range = 3.6, arc = 2.9, windup = 0.85, active = 0.32, recovery = 1.1 },
}

function Melee.start(actor, profile, dx, dy, damage)
  if actor.melee then
    return false
  end
  local spec = assert(Melee.profiles[profile])
  actor.melee = {
    profile = profile,
    angle = math.atan(dy, dx),
    elapsed = 0,
    damage = damage,
    hit = {},
    phase = "windup",
  }
  actor.cooldown = spec.windup + spec.active + spec.recovery
  return true
end

function Melee.angle(attack)
  local spec = Melee.profiles[attack.profile]
  local fraction = math.max(0, math.min(1, (attack.elapsed - spec.windup) / spec.active))
  return attack.angle - spec.arc / 2 + fraction * spec.arc
end

local function overlap(actor, target, first, last, range, geometry)
  local x, y = Body.muzzle(actor)
  local tx, ty = Body.muzzle(target)
  local dx, dy = tx - x, ty - y
  local distance = math.sqrt(dx * dx + dy * dy)
  local radius = Body.scale(target) * 0.4
  if distance > range + radius then
    return false
  end
  if distance <= radius then
    return true
  end
  local midpoint = (first + last) / 2
  local delta =
    math.atan(math.sin(math.atan(dy, dx) - midpoint), math.cos(math.atan(dy, dx) - midpoint))
  local allowance = math.asin(math.min(1, radius / distance))
  if math.abs(delta) > (last - first) / 2 + allowance then
    return false
  end
  return not geometry:wall_distance(x, y, dx / distance, dy / distance, distance)
end

function Melee.update(actor, targets, geometry, dt, damage)
  local attack = actor.melee
  if not attack then
    return
  end
  local spec = Melee.profiles[attack.profile]
  local before = attack.elapsed
  attack.elapsed = attack.elapsed + dt
  local active_end = spec.windup + spec.active
  if attack.elapsed >= spec.windup and before < active_end then
    local first = attack.angle
      - spec.arc / 2
      + math.max(0, (before - spec.windup) / spec.active) * spec.arc
    local last = attack.angle
      - spec.arc / 2
      + math.min(1, (attack.elapsed - spec.windup) / spec.active) * spec.arc
    for _, target in ipairs(targets) do
      if
        target.hp > 0
        and not attack.hit[target.id]
        and overlap(actor, target, first, last, spec.range, geometry)
      then
        attack.hit[target.id] = true
        damage(target, attack.damage)
      end
    end
  end
  if attack.elapsed < spec.windup then
    attack.phase = "windup"
  elseif attack.elapsed < active_end then
    attack.phase = "active"
  else
    attack.phase = "recovery"
  end
  if attack.elapsed >= active_end + spec.recovery then
    actor.melee = nil
  end
end

return Melee
