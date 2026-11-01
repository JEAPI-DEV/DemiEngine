-- A test policy using normal movement, equipment and combat actions.
local Floors = require("dungeon.floors")
local Navigator = require("tests.navigation")
local Combat = require("dungeon.combat")
local Body = require("dungeon.body")
local Melee = require("dungeon.melee")
local Physics = require("demi.physics.query2d")
local Driver = {}

function Driver.new()
  local nav = Navigator.new()
  return function(state)
    if state.phase == "upgrade" then
      return { upgrade = "power" }
    end
    if state.phase ~= "playing" then
      return {}
    end
    local floor = Floors.create(state.floor)
    if state.floor < 4 then
      return nav:action(state, floor.exit[1], floor.exit[2], true)
    end
    local enemy = state.enemies[1]
    local player = state.player
    local dx, dy, distance = Combat.direction(enemy.x - player.x, enemy.y - player.y)
    local ranged = player.mana >= 2 and distance > 3.5
    local desired = ranged and 4.8 or 1.2
    local action = nav:action(state, enemy.x, enemy.y, false)
    action.equip_slot = ranged and 4 or 1
    action.attack = true
    action.aim_x, action.aim_y = Body.muzzle(enemy)
    if distance < desired + 0.6 then
      local best, best_x, best_y
      for index = 0, 15 do
        local angle = index * math.pi / 8
        local mx, my = math.cos(angle), math.sin(angle)
        local x, y = player.x + mx, player.y + my
        if
          Floors.walkable(floor, x + 0.3, y + 0.3)
          and Floors.walkable(floor, x - 0.3, y - 0.3)
          and not Physics.raycast(player.x, player.y, mx, my, 1, "wall")
        then
          local _, _, gap = Combat.direction(enemy.x - x, enemy.y - y)
          local score = math.abs(gap - desired)
          if enemy.melee and enemy.melee.phase ~= "recovery" then
            local profile = Melee.profiles[enemy.melee.profile]
            local a = math.atan(y - enemy.y, x - enemy.x) - enemy.melee.angle
            a = math.atan(math.sin(a), math.cos(a))
            if gap < profile.range + 0.5 and math.abs(a) < profile.arc / 2 + 0.3 then
              score = score + 20
            end
          end
          for _, bolt in ipairs(state.projectiles) do
            if bolt.hostile then
              local separation = (bolt.x + bolt.dx * bolt.speed * 0.2 - x) ^ 2
                + (bolt.y + bolt.dy * bolt.speed * 0.2 - y - 0.55) ^ 2
              score = score + math.max(0, 1.2 - separation) * 12
            end
          end
          if not best or score < best then
            best, best_x, best_y = score, mx, my
          end
        end
      end
      action.move_x, action.move_y = best_x or 0, best_y or 0
    end
    if distance > desired + 0.6 then
      action.attack = ranged
    end
    return action
  end
end
return Driver
