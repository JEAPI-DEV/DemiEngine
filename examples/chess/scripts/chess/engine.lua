local Rules = require("chess.rules")

local Engine = {}
local MATE = 100000
local VALUES = { P = 100, N = 320, B = 330, R = 500, Q = 900, K = 0 }

local function piece_score(piece, square)
  local value = VALUES[piece:sub(2, 2)] or 0
  local file = ((square - 1) % 8) + 1
  local rank = math.floor((square - 1) / 8) + 1
  local center = 7 - math.abs(file - 4.5) - math.abs(rank - 4.5)
  local kind = piece:sub(2, 2)
  if kind == "N" or kind == "B" then value = value + center * 3 end
  if kind == "P" then
    local advancement = piece:sub(1, 1) == "w" and rank - 2 or 7 - rank
    value = value + advancement * 5
  end
  return value
end

local function evaluate(state)
  local white = 0
  for square = 1, 64 do
    local piece = state.board[square]
    if piece then
      local value = piece_score(piece, square)
      white = white + (piece:sub(1, 1) == "w" and value or -value)
    end
  end
  return state.turn == "w" and white or -white
end

local function ordered_moves(state)
  local moves = Rules.legal_moves(state)
  table.sort(moves, function(left, right)
    local left_score = (left.promotion and VALUES[left.promotion] or 0) +
      (left.capture and VALUES[left.capture:sub(2, 2)] * 10 or 0) +
      (left.castle_rook_from and 50 or 0)
    local right_score = (right.promotion and VALUES[right.promotion] or 0) +
      (right.capture and VALUES[right.capture:sub(2, 2)] * 10 or 0) +
      (right.castle_rook_from and 50 or 0)
    if left_score ~= right_score then return left_score > right_score end
    return Rules.move_key(left) < Rules.move_key(right)
  end)
  return moves
end

local function search(state, depth, alpha, beta, ply, stats)
  stats.nodes = stats.nodes + 1
  local moves = ordered_moves(state)
  if #moves == 0 then
    if Rules.in_check(state, state.turn) then return -MATE + ply end
    return 0
  end
  if Rules.draw_reason(state) then return 0 end
  if depth == 0 then return evaluate(state) end
  local best = -math.huge
  for _, move in ipairs(moves) do
    local score = -search(Rules.apply(state, move, false), depth - 1, -beta, -alpha, ply + 1, stats)
    if score > best then best = score end
    if score > alpha then alpha = score end
    if alpha >= beta then
      stats.cutoffs = stats.cutoffs + 1
      break
    end
  end
  return best
end

function Engine.choose_move(state, depth)
  depth = math.max(1, math.min(4, math.floor(tonumber(depth) or 2)))
  local moves = ordered_moves(state)
  local stats = { nodes = 0, cutoffs = 0, depth = depth, score = 0 }
  if #moves == 0 then return nil, stats end
  local best_move, best_score = nil, -math.huge
  for _, move in ipairs(moves) do
    local score = -search(Rules.apply(state, move, false), depth - 1,
      -math.huge, -best_score, 1, stats)
    if score > best_score then
      best_move, best_score = move, score
    end
  end
  stats.score = best_score
  return best_move, stats
end

return Engine
