local Player = {}

function Player:on_create()
  Debug.log("Player created")
end

function Player:on_start()
  Debug.log("Use WASD or arrow keys to move the player")
end

local function movement_axis()
  local x = 0.0
  local y = 0.0

  if Input.is_down("a") or Input.is_down("left") then
    x = x - 1.0
  end
  if Input.is_down("d") or Input.is_down("right") then
    x = x + 1.0
  end
  if Input.is_down("s") or Input.is_down("down") then
    y = y - 1.0
  end
  if Input.is_down("w") or Input.is_down("up") then
    y = y + 1.0
  end

  if x ~= 0.0 and y ~= 0.0 then
    local diagonal = 0.70710678
    x = x * diagonal
    y = y * diagonal
  end

  return x, y
end

function Player:on_update(dt)
  local x, y = movement_axis()
  if x == 0.0 and y == 0.0 then
    return
  end

  Entity.add_position(self.entity_id, x * self.speed * dt, y * self.speed * dt)
end

function Player:on_fixed_update(dt)
end

function Player:on_destroy()
  Debug.log("Player destroyed")
end

return Player
