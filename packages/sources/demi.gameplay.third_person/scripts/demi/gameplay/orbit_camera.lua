local Orbit = {}
Orbit.__index = Orbit

function Orbit.new(options)
  options = options or {}
  return setmetatable({ yaw = options.yaw or math.pi, pitch = options.pitch or 0.35,
    distance = options.distance or 6, height = options.height or 0.7,
    sensitivity = options.sensitivity or 0.003, radius = options.radius or 0.25,
    clearance = options.clearance or 0.08 }, Orbit)
end

function Orbit:look(dx, dy, target, lock_target)
  if lock_target then
    local x, z = lock_target.x-target.x, lock_target.z-target.z
    if x*x+z*z > 0.000001 then self.yaw = math.atan(x,z) end
  else
    self.yaw = self.yaw - dx*self.sensitivity
  end
  self.pitch = math.max(-0.1, math.min(1.1, self.pitch + dy*self.sensitivity))
  return math.sin(self.yaw), math.cos(self.yaw)
end

-- cast(origin, radius, direction, distance) returns nearest blocking distance.
-- Clamp to the actual obstruction; a minimum boom length can push through walls.
function Orbit:position(target, cast)
  local origin = { x=target.x, y=target.y+self.height, z=target.z }
  local direction = { x=-math.sin(self.yaw)*math.cos(self.pitch),
    y=math.sin(self.pitch), z=-math.cos(self.yaw)*math.cos(self.pitch) }
  local distance = self.distance
  if cast then
    local hit = cast(origin, self.radius, direction, distance)
    if hit then distance = math.max(0, math.min(distance, hit-self.clearance)) end
  end
  return { x=origin.x+direction.x*distance, y=origin.y+direction.y*distance,
    z=origin.z+direction.z*distance }, origin
end

return Orbit

