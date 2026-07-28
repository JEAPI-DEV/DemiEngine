local TopDownController2D = {}
TopDownController2D.__index = TopDownController2D

local function normalized(x, y)
  local length = math.sqrt(x * x + y * y)
  if length > 1.0 then
    return x / length, y / length
  end
  return x, y
end

function TopDownController2D.new(options)
  options = options or {}
  return setmetatable({
    move_action = options.move_action or "move",
    move_x_action = options.move_x_action or "move_x",
    move_y_action = options.move_y_action or "move_y",
    speed = options.speed or 5.0,
    rotate = options.rotate == true,
    flip_sprite = options.flip_sprite == true,
  }, TopDownController2D)
end

function TopDownController2D:update(entity_id)
  local x, y = Input.action_vector(self.move_action)
  if x == 0.0 and y == 0.0 then
    x = Input.action_value(self.move_x_action)
    y = Input.action_value(self.move_y_action)
  end
  x, y = normalized(x, y)
  Rigidbody2D.set_velocity(entity_id, x * self.speed, y * self.speed)
  if math.abs(x) + math.abs(y) > 0.001 then
    if self.rotate then
      Transform.set_rotation(entity_id, math.atan(y, x))
    end
    if self.flip_sprite then
      Sprite2D.set_flip(entity_id, x < 0.0, false)
    end
  end
  return x, y
end

return TopDownController2D
