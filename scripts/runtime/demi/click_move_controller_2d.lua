local ClickMoveController2D = {}
ClickMoveController2D.__index = ClickMoveController2D

function ClickMoveController2D.new(options)
  options = options or {}
  return setmetatable({
    move_action = options.move_action or "move_to",
    speed = options.speed or 4.0,
    arrival_distance = options.arrival_distance or 0.08,
    diagonal = options.diagonal ~= false,
    path = {},
    waypoint = 1,
  }, ClickMoveController2D)
end

function ClickMoveController2D:request(entity_id, world_x, world_y)
  local x, y = Transform.get_position(entity_id)
  local start_x, start_y = Navigation2D.world_to_cell(x, y)
  local goal_x, goal_y = Navigation2D.world_to_cell(world_x, world_y)
  if start_x == nil or goal_x == nil then
    return false, "PATH_OUT_OF_BOUNDS"
  end
  local path, diagnostic = Navigation2D.path(
    start_x, start_y, goal_x, goal_y, self.diagonal
  )
  self.path = path or {}
  self.waypoint = #self.path > 1 and 2 or 1
  return diagnostic == "OK", diagnostic
end

function ClickMoveController2D:update(entity_id)
  if Input.action_pressed(self.move_action) then
    local target_x, target_y = Input.mouse_world_position()
    if target_x ~= nil then
      self:request(entity_id, target_x, target_y)
    end
  end

  local waypoint = self.path[self.waypoint]
  if waypoint == nil then
    Rigidbody2D.set_velocity(entity_id, 0.0, 0.0)
    return false
  end
  local x, y = Transform.get_position(entity_id)
  local dx = waypoint.world_x - x
  local dy = waypoint.world_y - y
  local distance = math.sqrt(dx * dx + dy * dy)
  if distance <= self.arrival_distance then
    self.waypoint = self.waypoint + 1
    return self:update(entity_id)
  end
  Rigidbody2D.set_velocity(
    entity_id, dx / distance * self.speed, dy / distance * self.speed
  )
  return true
end

return ClickMoveController2D
