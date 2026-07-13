local CharacterController2D = {}
CharacterController2D.__index = CharacterController2D

function CharacterController2D.new(options)
  options = options or {}
  return setmetatable({
    move_action = options.move_action or "move",
    jump_action = options.jump_action or "jump",
    ground_layer = options.ground_layer or "platform",
    move_speed = options.move_speed or 5.0,
    jump_speed = options.jump_speed or 9.0,
    ground_distance = options.ground_distance or 0.56,
    flip_sprite = options.flip_sprite ~= false,
    idle_animation = options.idle_animation or "idle",
    run_animation = options.run_animation or "run",
  }, CharacterController2D)
end

function CharacterController2D:is_grounded(entity_id)
  local x, y = Entity.get_position(entity_id)
  if x == nil or y == nil then
    return false
  end
  return Physics2D.raycast(
    x, y, 0.0, -1.0, self.ground_distance, self.ground_layer, entity_id
  ) ~= nil
end

function CharacterController2D:update_horizontal(entity_id)
  local axis = Input.action_value(self.move_action)
  Rigidbody2D.set_velocity_x(entity_id, axis * self.move_speed)
  if self.flip_sprite and axis ~= 0.0 then
    Sprite2D.set_flip(entity_id, axis < 0.0, false)
  end
  Sprite2D.play_animation(
    entity_id, axis == 0.0 and self.idle_animation or self.run_animation
  )
  return axis
end

function CharacterController2D:try_jump(entity_id, grounded)
  if grounded and Input.action_pressed(self.jump_action) then
    Rigidbody2D.set_velocity_y(entity_id, self.jump_speed)
    return true
  end
  return false
end

return CharacterController2D
