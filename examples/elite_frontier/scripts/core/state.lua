local Galaxy = require("sim.galaxy")

local State = {
  mode = "station",
  commander = {
    credits = 120,
    fuel = 7,
    hold_capacity = 12,
    cargo = {
      food = 2,
      machines = 0,
      medicine = 0,
    },
  },
  ship = {
    speed = 0,
    max_speed = 84,
    yaw = 0,
    pitch = 0,
    roll = 0,
    station_distance = 0,
    alert = "GREEN",
  },
  selected_system = 2,
  system_index = 1,
  message = "Docked at Coriolis station.",
}

function State.boot()
  State.systems = Galaxy.generate_cluster()
  State.system_index = 1
  State.selected_system = 2
  State.mode = "station"
  State.ship.station_distance = 0
  State.ship.speed = 0
end

function State.current_system()
  return State.systems[State.system_index]
end

function State.target_system()
  return State.systems[State.selected_system]
end

function State.cargo_used()
  local total = 0
  for _, amount in pairs(State.commander.cargo) do
    total = total + amount
  end
  return total
end

function State.cargo_free()
  return State.commander.hold_capacity - State.cargo_used()
end

function State.advance_target()
  State.selected_system = State.selected_system + 1
  if State.selected_system > #State.systems then
    State.selected_system = 1
  end
  if State.selected_system == State.system_index then
    State.advance_target()
  end
end

return State
