local Navigation = require("demi.navigation")
local Floors = require("dungeon.floors")
local Exploration = require("dungeon.exploration")
local Combat = require("dungeon.combat")
local Melee = require("dungeon.melee")
local Navigator = {}
Navigator.__index = Navigator

function Navigator.new()
  return setmetatable({}, Navigator)
end

function Navigator:direction(state, x, y)
  local signature = tostring(state.floor)
  for _, gate in ipairs(state.gates) do
    signature = signature .. tostring(gate.open)
  end
  if self.signature ~= signature then
    self.signature = signature
    local floor = Floors.create(state.floor)
    self.grid = assert(Navigation.create_grid(floor.width, floor.height, 1, -0.5, -0.5))
    for row = 0, floor.height - 1 do
      for column = 0, floor.width - 1 do
        local blocked = not Floors.walkable(floor, column, row)
        for _, gate in ipairs(state.gates) do
          blocked = blocked or (not gate.open and Exploration.gate_contains(gate, column, row))
        end
        self.grid:set_blocked(column, row, blocked)
      end
    end
    for _, trap in ipairs(floor.traps) do
      for row = math.ceil(trap.y - trap.height / 2), math.floor(trap.y + trap.height / 2) do
        for column = math.ceil(trap.x - trap.width / 2), math.floor(trap.x + trap.width / 2) do
          self.grid:set_cost(column, row, 5)
        end
      end
    end
    self.route_time = -1
  end
  local player = state.player
  local _, _, distance = Combat.direction(x - player.x, y - player.y)
  if distance < 0.35 then
    return 0, 0
  end
  if state.time > self.route_time or x ~= self.target_x or y ~= self.target_y then
    self.path = self.grid:path(
      math.floor(player.x + 0.5),
      math.floor(player.y + 0.5),
      math.floor(x + 0.5),
      math.floor(y + 0.5),
      false
    )
    self.route_time = state.time + 0.15
    self.target_x, self.target_y = x, y
  end
  local point = self.path and self.path[2]
  return Combat.direction(
    (point and point.world_x or x) - player.x,
    (point and point.world_y or y) - player.y
  )
end

function Navigator:action(state, x, y, interact)
  local dx, dy = self:direction(state, x, y)
  local action = {
    move_x = dx,
    move_y = dy,
    interact = interact,
    heal = state.player.hp <= state.player.max_hp - 4,
  }
  for _, enemy in ipairs(state.enemies) do
    if enemy.hp > 0 and enemy.melee and enemy.melee.phase ~= "recovery" then
      local profile = Melee.profiles[enemy.melee.profile]
      local _, _, distance = Combat.direction(enemy.x - state.player.x, enemy.y - state.player.y)
      if distance < profile.range + 0.6 and profile.windup - enemy.melee.elapsed < 0.06 then
        action.dash = true
      end
    end
  end
  return action
end

function Navigator.travel(Test, Session, x, y, timeout)
  local nav = Navigator.new()
  Session.set_driver(function(state)
    return nav:action(state, x, y, false)
  end)
  for attempt = 1, math.ceil((timeout or 15) / 0.05) do
    local state = Session.observe()
    Test.expect(state.phase ~= "dead", "Travel died before " .. x .. "," .. y)
    if (state.player.x - x) ^ 2 + (state.player.y - y) ^ 2 < 0.6 ^ 2 then
      Session.set_driver(nil)
      return
    end
    Test.wait(0.05)
  end
  Session.set_driver(nil)
  Test.expect(false, "Could not reach " .. x .. "," .. y)
end

return Navigator
