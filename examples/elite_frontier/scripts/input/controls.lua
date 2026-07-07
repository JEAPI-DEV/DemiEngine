local Controls = {
  touch_throttle = 0,
}

local function touch_axis(width, height)
  if not Input.mouse_down("left") then
    return 0, 0, 0
  end
  local x, y = Input.mouse_position()
  local turn = 0
  local throttle = 0
  local dock = 0
  if y > height * 0.58 and x < width * 0.22 then
    turn = x < width * 0.11 and -1 or 1
  elseif y > height * 0.58 and x > width * 0.78 then
    if x < width * 0.90 then
      throttle = 1
    else
      dock = 1
    end
  end
  return turn, throttle, dock
end

function Controls.sample()
  local width, height = Input.viewport_size()
  local touch_turn, touch_throttle, touch_dock = touch_axis(width, height)
  local turn = Input.axis("a", "d") + touch_turn
  local throttle = Input.axis("s", "w") + touch_throttle
  local pitch = Input.axis("q", "e")
  return {
    turn = math.max(-1, math.min(1, turn)),
    throttle = math.max(-1, math.min(1, throttle)),
    pitch = math.max(-1, math.min(1, pitch)),
    dock = Input.is_pressed("return") or touch_dock > 0,
    jump = Input.is_pressed("j"),
  }
end

return Controls
