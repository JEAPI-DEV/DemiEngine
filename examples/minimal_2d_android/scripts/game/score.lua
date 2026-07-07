local state = require("game_state")
local hud = require("game.hud")

local Score = {}

function Score.reset(game)
  game.points = 0
  game.score_origin_x = nil
  game.score_origin_y = nil
  game.best_distance = 0.0
end

function Score.reset_local(game)
  Score.reset(game)
  state.extra_jumps = 0
  hud.update_points(game.points)
  hud.update_extra_jumps(true)
end

function Score.update_distance(game, player_x, player_y)
  if game.score_origin_x == nil or game.score_origin_y == nil then
    game.score_origin_x = player_x
    game.score_origin_y = player_y
  end

  local distance = player_x - game.score_origin_x
  if game.score_axis == "y" then
    distance = player_y - game.score_origin_y
  end

  game.best_distance = math.max(game.best_distance, distance, 0.0)
  local points = math.floor(game.best_distance)
  if points ~= game.points then
    game.points = points
    hud.update_points(game.points)
  end
end

return Score
