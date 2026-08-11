local Controllers = {}

function Controllers.platform(input, state, config)
  local intent = { x = input.x or 0, jump = false }
  local grace = state.grounded and (config.coyote_time or 0) or math.max(0, (state.coyote or 0) - input.dt)
  if state.grounded then grace = config.coyote_time or 0 end
  local buffer = input.jump and (config.jump_buffer or 0) or math.max(0, (state.jump_buffer or 0) - input.dt)
  if buffer > 0 and grace > 0 then intent.jump, buffer, grace = true, 0, 0 end
  return intent, { coyote = grace, jump_buffer = buffer, grounded = state.grounded }
end

function Controllers.top_down(input)
  local x, y = input.x or 0, input.y or 0; local length = math.sqrt(x * x + y * y)
  if length > 1 then x, y = x / length, y / length end
  return { x = x, y = y, aim_x = input.aim_x, aim_y = input.aim_y }
end

function Controllers.click_to_move(input)
  return input.clicked and { target_x = input.x, target_y = input.y } or nil
end

function Controllers.isometric(input)
  return { grid_x = (input.right or 0) - (input.left or 0),
    grid_y = (input.down or 0) - (input.up or 0) }
end

return Controllers
