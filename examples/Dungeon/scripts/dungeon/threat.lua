local Floors = require("dungeon.floors")
local Threat = {}

function Threat.provoke(run, victim)
  local room = Floors.room(run.floor, victim.x, victim.y)
  for _, enemy in ipairs(run.enemies) do
    local nearby = (enemy.x - victim.x) ^ 2 + (enemy.y - victim.y) ^ 2 <= 4.5 ^ 2
    local same_room = Floors.room(run.floor, enemy.x, enemy.y) == room
    if enemy.hp > 0 and (enemy == victim or (nearby and same_room)) then
      local newly_alerted = (enemy.alert_time or 0) == 0
      enemy.awakened = true
      enemy.provoked = true
      enemy.alert_time = 10
      enemy.last_seen_x, enemy.last_seen_y = run.player.x, run.player.y
      enemy.path_timer = 0
      if newly_alerted and (enemy.kind == "skirmisher" or enemy.kind == "crawler") then
        enemy.charge_windup = 0.3
      end
    end
  end
end

return Threat
