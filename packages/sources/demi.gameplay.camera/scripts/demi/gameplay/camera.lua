local Camera = {}
Camera.__index = Camera

function Camera.new(values)
  values = values or {}
  return setmetatable({ x = values.x or 0, y = values.y or 0, smoothing = values.smoothing or 10,
    look_ahead = values.look_ahead or 0, zones = {}, shakes = {} }, Camera)
end

function Camera:set_zone(id, values) values.id = id; self.zones[id] = values end
function Camera:remove_zone(id) self.zones[id] = nil end
function Camera:shake(amplitude, duration) table.insert(self.shakes, { amplitude = amplitude, left = duration }) end

function Camera:update(target, velocity, dt, random)
  local desired_x = target.x + (velocity.x or 0) * self.look_ahead
  local desired_y = target.y + (velocity.y or 0) * self.look_ahead
  local selected
  for _, zone in pairs(self.zones) do
    if target.x >= zone.left and target.x <= zone.right and target.y >= zone.top and target.y <= zone.bottom and
      (not selected or (zone.priority or 0) > (selected.priority or 0) or
       (zone.priority or 0) == (selected.priority or 0) and zone.id < selected.id) then selected = zone end
  end
  if selected then
    desired_x = math.max(selected.left, math.min(selected.right, desired_x))
    desired_y = math.max(selected.top, math.min(selected.bottom, desired_y))
  end
  local blend = math.min(1, self.smoothing * dt)
  self.x, self.y = self.x + (desired_x - self.x) * blend, self.y + (desired_y - self.y) * blend
  local shake_x, shake_y = 0, 0
  for index = #self.shakes, 1, -1 do
    local shake = self.shakes[index]; shake.left = shake.left - dt
    if shake.left <= 0 then table.remove(self.shakes, index)
    else
      shake_x = shake_x + ((random and random() or 0.5) * 2 - 1) * shake.amplitude
      shake_y = shake_y + ((random and random() or 0.5) * 2 - 1) * shake.amplitude
    end
  end
  return { x = self.x + shake_x, y = self.y + shake_y, zone = selected and selected.id }
end

return Camera
