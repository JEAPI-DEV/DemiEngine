local Galaxy = require("sim.galaxy")

local Ship = {}

function Ship.launch(state)
  if state.mode ~= "station" then
    return
  end
  state.mode = "flight"
  state.ship.station_distance = 4.5
  state.ship.speed = 14
  state.ship.alert = "GREEN"
  state.message = "Launch clearance granted."
end

function Ship.try_dock(state)
  if state.mode ~= "flight" then
    return false
  end
  if state.ship.station_distance > 2.2 or state.ship.speed > 32 then
    state.message = "Docking denied: slow down and close range."
    return false
  end
  state.mode = "station"
  state.ship.speed = 0
  state.ship.station_distance = 0
  state.message = "Docking complete."
  return true
end

function Ship.jump(state)
  local from = state.current_system()
  local target = state.target_system()
  local distance = Galaxy.distance(from, target)
  local fuel_cost = math.max(1, math.ceil(distance / 16))
  if state.commander.fuel < fuel_cost then
    state.message = "Not enough fuel for jump."
    return false
  end
  state.commander.fuel = state.commander.fuel - fuel_cost
  state.system_index = state.selected_system
  state.advance_target()
  state.mode = "station"
  state.ship.speed = 0
  state.ship.station_distance = 0
  state.message = "Arrived at " .. state.current_system().name .. "."
  return true
end

function Ship.update(state, controls, dt)
  if state.mode ~= "flight" then
    return
  end
  state.ship.speed = math.max(0, math.min(state.ship.max_speed, state.ship.speed + controls.throttle * 48 * dt))
  state.ship.yaw = state.ship.yaw + controls.turn * 1.8 * dt
  state.ship.pitch = math.max(-0.7, math.min(0.7, state.ship.pitch + controls.pitch * 1.2 * dt))
  state.ship.roll = state.ship.roll + (controls.turn * 1.7 - state.ship.roll) * math.min(1, dt * 6)

  local drift = math.cos(state.ship.yaw) * state.ship.speed * dt * 0.018
  state.ship.station_distance = math.max(0.5, state.ship.station_distance + drift)
  if state.ship.station_distance > 8 then
    state.ship.alert = "YELLOW"
  else
    state.ship.alert = "GREEN"
  end
end

return Ship
