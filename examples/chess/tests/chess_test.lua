local Rules = require("chess.rules")
local Engine = require("chess.engine")

local function find_move(state, from, to, promotion)
  from, to = Rules.square(from), Rules.square(to)
  for _, move in ipairs(Rules.legal_moves(state, from)) do
    if move.to == to and (not promotion or move.promotion == promotion) then return move end
  end
  return nil
end

local function play(state, from, to, promotion)
  local next_state, _, move_error = Rules.play(state, from, to, promotion)
  if not next_state then error("expected legal move " .. from .. to .. ": " .. tostring(move_error)) end
  return next_state
end

Test.case("initial position has twenty legal moves and applying is immutable", function()
  local state = Rules.new_game()
  Test.equal(#Rules.legal_moves(state), 20)
  local next_state = play(state, "e2", "e4")
  Test.equal(Rules.piece_at(state, "e2"), "wP")
  Test.equal(Rules.piece_at(next_state, "e2"), nil)
  Test.equal(Rules.piece_at(next_state, "e4"), "wP")
  Test.equal(next_state.turn, "b")
end)

Test.case("a pinned piece cannot expose its king", function()
  local state = Rules.from_fen("4r1k1/8/8/8/8/8/4R3/4K3 w - - 0 1")
  Test.equal(find_move(state, "e2", "d2"), nil)
  Test.equal(find_move(state, "e2", "e8") ~= nil, true)
end)

Test.case("castling moves the rook and cannot cross an attacked square", function()
  local state = Rules.from_fen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1")
  local castle = find_move(state, "e1", "g1")
  Test.equal(castle ~= nil, true)
  local castled = Rules.apply(state, castle)
  Test.equal(Rules.piece_at(castled, "g1"), "wK")
  Test.equal(Rules.piece_at(castled, "f1"), "wR")
  Test.equal(castled.castling.wk, false)
  Test.equal(castled.castling.wq, false)

  local attacked = Rules.from_fen("r3kr1r/8/8/8/8/8/8/R3K2R w KQq - 0 1")
  Test.equal(find_move(attacked, "e1", "g1"), nil)
  Test.equal(find_move(attacked, "e1", "c1") ~= nil, true)
end)

Test.case("castling works from a normal game after clearing the path", function()
  local state = Rules.new_game()
  state = play(state, "e2", "e4")
  state = play(state, "a7", "a6")
  state = play(state, "g1", "f3")
  state = play(state, "a6", "a5")
  state = play(state, "f1", "e2")
  state = play(state, "b7", "b6")
  local castle = find_move(state, "e1", "g1")
  Test.equal(castle ~= nil, true)
  state = Rules.apply(state, castle)
  Test.equal(Rules.piece_at(state, "g1"), "wK")
  Test.equal(Rules.piece_at(state, "f1"), "wR")
end)

Test.case("black can castle and rook movement permanently loses rights", function()
  local black = Rules.from_fen("r3k2r/8/8/8/8/8/8/4K3 b kq - 0 1")
  local black_castle = find_move(black, "e8", "g8")
  Test.equal(black_castle ~= nil, true)
  black = Rules.apply(black, black_castle)
  Test.equal(Rules.piece_at(black, "g8"), "bK")
  Test.equal(Rules.piece_at(black, "f8"), "bR")

  local state = Rules.from_fen("4k3/8/8/8/8/8/8/R3K2R w KQ - 0 1")
  state = play(state, "h1", "h2")
  state = play(state, "e8", "e7")
  state = play(state, "h2", "h1")
  state = play(state, "e7", "e8")
  Test.equal(find_move(state, "e1", "g1"), nil)
  Test.equal(find_move(state, "e1", "c1") ~= nil, true)
end)

Test.case("en passant removes the passed pawn and expires", function()
  local state = Rules.from_fen("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1")
  local capture = find_move(state, "e5", "d6")
  Test.equal(capture ~= nil, true)
  local captured = Rules.apply(state, capture)
  Test.equal(Rules.piece_at(captured, "d5"), nil)
  Test.equal(Rules.piece_at(captured, "d6"), "wP")

  local expired = play(state, "e1", "e2")
  Test.equal(expired.en_passant, nil)
end)

Test.case("en passant cannot expose its own king", function()
  local state = Rules.from_fen("k3r3/8/8/3pP3/8/8/8/4K3 w - d6 0 1")
  Test.equal(find_move(state, "e5", "d6"), nil)
end)

Test.case("promotion provides every standard piece", function()
  local state = Rules.from_fen("4k3/P7/8/8/8/8/8/4K3 w - - 0 1")
  local promotions = {}
  for _, move in ipairs(Rules.legal_moves(state, "a7")) do
    if move.to == Rules.square("a8") then promotions[move.promotion] = true end
  end
  Test.equal(promotions.Q and promotions.R and promotions.B and promotions.N, true)
  local promoted = play(state, "a7", "a8", "N")
  Test.equal(Rules.piece_at(promoted, "a8"), "wN")
end)

Test.case("fools mate is checkmate rather than merely check", function()
  local state = Rules.new_game()
  state = play(state, "f2", "f3")
  state = play(state, "e7", "e5")
  state = play(state, "g2", "g4")
  state = play(state, "d8", "h4")
  local status = Rules.status(state)
  Test.equal(status.kind, "checkmate")
  Test.equal(status.winner, "b")
  Test.equal(#status.legal_moves, 0)
end)

Test.case("kings cannot move adjacent to one another", function()
  local state = Rules.from_fen("8/8/8/8/8/4k3/8/4K3 w - - 0 1")
  Test.equal(find_move(state, "e1", "e2"), nil)
end)

Test.case("stalemate and draw edge cases are recognized", function()
  local stalemate = Rules.status(Rules.from_fen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1"))
  Test.equal(stalemate.kind, "stalemate")
  local material = Rules.status(Rules.from_fen("4k3/8/8/8/8/8/8/4K3 w - - 0 1"))
  Test.equal(material.kind, "draw")
  Test.equal(material.reason, "insufficient material")
  local fifty = Rules.status(Rules.from_fen("4k3/8/8/8/8/8/4R3/4K3 w - - 100 1"))
  Test.equal(fifty.kind, "draw")
  Test.equal(fifty.reason, "fifty-move rule")
end)

Test.case("threefold repetition uses the complete position state", function()
  local state = Rules.new_game()
  for _ = 1, 2 do
    state = play(state, "g1", "f3")
    state = play(state, "g8", "f6")
    state = play(state, "f3", "g1")
    state = play(state, "f6", "g8")
  end
  local status = Rules.status(state)
  Test.equal(status.kind, "draw")
  Test.equal(status.reason, "threefold repetition")
end)

Test.case("computer search is deterministic and always returns a legal move", function()
  local state = play(Rules.new_game(), "e2", "e4")
  local first, first_stats = Engine.choose_move(state, 2)
  local second = Engine.choose_move(state, 2)
  Test.equal(first ~= nil, true)
  Test.equal(Rules.move_key(first), Rules.move_key(second))
  Test.equal(find_move(state, Rules.square_name(first.from), Rules.square_name(first.to),
    first.promotion) ~= nil, true)
  Test.equal(first_stats.nodes > 0, true)
end)
