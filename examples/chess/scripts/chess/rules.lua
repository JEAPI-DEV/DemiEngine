local Rules = {}

local FILES = "abcdefgh"
local PROMOTIONS = { "Q", "R", "B", "N" }
local KNIGHT_STEPS = {
  { 1, 2 }, { 2, 1 }, { 2, -1 }, { 1, -2 },
  { -1, -2 }, { -2, -1 }, { -2, 1 }, { -1, 2 },
}
local KING_STEPS = {
  { 1, 0 }, { 1, 1 }, { 0, 1 }, { -1, 1 },
  { -1, 0 }, { -1, -1 }, { 0, -1 }, { 1, -1 },
}
local BISHOP_DIRECTIONS = { { 1, 1 }, { -1, 1 }, { 1, -1 }, { -1, -1 } }
local ROOK_DIRECTIONS = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }

local function index(file, rank)
  if file < 1 or file > 8 or rank < 1 or rank > 8 then return nil end
  return (rank - 1) * 8 + file
end

local function coordinates(square)
  return ((square - 1) % 8) + 1, math.floor((square - 1) / 8) + 1
end

local function opposite(side) return side == "w" and "b" or "w" end
local function side_of(piece) return piece and piece:sub(1, 1) or nil end
local function kind_of(piece) return piece and piece:sub(2, 2) or nil end

function Rules.square(name)
  if type(name) == "number" then
    return name >= 1 and name <= 64 and name or nil
  end
  if type(name) ~= "string" or #name ~= 2 then return nil end
  local file = FILES:find(name:sub(1, 1), 1, true)
  local rank = tonumber(name:sub(2, 2))
  return file and rank and index(file, rank) or nil
end

function Rules.square_name(square)
  if type(square) ~= "number" or square < 1 or square > 64 then return nil end
  local file, rank = coordinates(square)
  return FILES:sub(file, file) .. tostring(rank)
end

local function clone_table(source)
  local result = {}
  for key, value in pairs(source or {}) do result[key] = value end
  return result
end

function Rules.clone(state)
  return {
    board = clone_table(state.board),
    turn = state.turn,
    castling = clone_table(state.castling),
    en_passant = state.en_passant,
    halfmove = state.halfmove,
    fullmove = state.fullmove,
    position_counts = clone_table(state.position_counts),
  }
end

local function blank_state()
  return {
    board = {}, turn = "w",
    castling = { wk = false, wq = false, bk = false, bq = false },
    en_passant = nil, halfmove = 0, fullmove = 1, position_counts = {},
  }
end

function Rules.position_key(state)
  local cells = {}
  for square = 1, 64 do cells[square] = state.board[square] or "--" end
  local rights = ""
  if state.castling.wk then rights = rights .. "K" end
  if state.castling.wq then rights = rights .. "Q" end
  if state.castling.bk then rights = rights .. "k" end
  if state.castling.bq then rights = rights .. "q" end
  local en_passant = "-"
  if state.en_passant then
    local file, rank = coordinates(state.en_passant)
    local pawn_rank = rank + (state.turn == "w" and -1 or 1)
    for _, file_offset in ipairs({ -1, 1 }) do
      local pawn = index(file + file_offset, pawn_rank)
      if pawn and state.board[pawn] == state.turn .. "P" then
        en_passant = Rules.square_name(state.en_passant)
        break
      end
    end
  end
  return table.concat(cells, "") .. ":" .. state.turn .. ":" ..
    (rights ~= "" and rights or "-") .. ":" ..
    en_passant
end

function Rules.new_game()
  local state = blank_state()
  local back_rank = { "R", "N", "B", "Q", "K", "B", "N", "R" }
  for file = 1, 8 do
    state.board[index(file, 1)] = "w" .. back_rank[file]
    state.board[index(file, 2)] = "wP"
    state.board[index(file, 7)] = "bP"
    state.board[index(file, 8)] = "b" .. back_rank[file]
  end
  state.castling = { wk = true, wq = true, bk = true, bq = true }
  state.position_counts[Rules.position_key(state)] = 1
  return state
end

function Rules.from_fen(fen)
  assert(type(fen) == "string", "FEN must be a string")
  local placement, turn, castling, en_passant, halfmove, fullmove =
    fen:match("^(%S+)%s+(%S+)%s+(%S+)%s+(%S+)%s+(%d+)%s+(%d+)$")
  assert(placement and (turn == "w" or turn == "b"), "invalid FEN")
  local state, rank = blank_state(), 8
  for row in placement:gmatch("[^/]+") do
    local file = 1
    for token in row:gmatch(".") do
      local empty = tonumber(token)
      if empty then
        file = file + empty
      else
        local side = token:match("%u") and "w" or "b"
        local kind = token:upper()
        assert(kind:match("[KQRBNP]") and file <= 8, "invalid FEN placement")
        state.board[index(file, rank)] = side .. kind
        file = file + 1
      end
    end
    assert(file == 9, "invalid FEN rank width")
    rank = rank - 1
  end
  assert(rank == 0, "invalid FEN rank count")
  state.turn = turn
  state.castling = {
    wk = castling:find("K", 1, true) ~= nil,
    wq = castling:find("Q", 1, true) ~= nil,
    bk = castling:find("k", 1, true) ~= nil,
    bq = castling:find("q", 1, true) ~= nil,
  }
  state.en_passant = en_passant ~= "-" and Rules.square(en_passant) or nil
  assert(en_passant == "-" or state.en_passant, "invalid FEN en passant square")
  state.halfmove, state.fullmove = tonumber(halfmove), tonumber(fullmove)
  state.position_counts[Rules.position_key(state)] = 1
  return state
end

function Rules.piece_at(state, square)
  return state.board[Rules.square(square)]
end

local function add_move(moves, from, to, extra)
  local move = { from = from, to = to }
  for key, value in pairs(extra or {}) do move[key] = value end
  moves[#moves + 1] = move
end

local function add_step_moves(state, moves, from, side, steps)
  local file, rank = coordinates(from)
  for _, step in ipairs(steps) do
    local to = index(file + step[1], rank + step[2])
    local target = to and state.board[to]
    if to and side_of(target) ~= side and kind_of(target) ~= "K" then
      add_move(moves, from, to, target and { capture = target } or nil)
    end
  end
end

local function add_sliding_moves(state, moves, from, side, directions)
  local file, rank = coordinates(from)
  for _, direction in ipairs(directions) do
    local distance = 1
    while true do
      local to = index(file + direction[1] * distance, rank + direction[2] * distance)
      if not to then break end
      local target = state.board[to]
      if not target then
        add_move(moves, from, to)
      else
        if side_of(target) ~= side and kind_of(target) ~= "K" then
          add_move(moves, from, to, { capture = target })
        end
        break
      end
      distance = distance + 1
    end
  end
end

local function add_pawn_move(moves, from, to, promotion_rank, extra)
  local _, rank = coordinates(to)
  if rank == promotion_rank then
    for _, promotion in ipairs(PROMOTIONS) do
      local details = clone_table(extra)
      details.promotion = promotion
      add_move(moves, from, to, details)
    end
  else
    add_move(moves, from, to, extra)
  end
end

local function pseudo_moves(state, side)
  local moves = {}
  for from = 1, 64 do
    local piece = state.board[from]
    if side_of(piece) == side then
      local kind, file, rank = kind_of(piece), coordinates(from)
      if kind == "P" then
        local direction = side == "w" and 1 or -1
        local home_rank = side == "w" and 2 or 7
        local promotion_rank = side == "w" and 8 or 1
        local one = index(file, rank + direction)
        if one and not state.board[one] then
          add_pawn_move(moves, from, one, promotion_rank)
          local two = index(file, rank + direction * 2)
          if rank == home_rank and two and not state.board[two] then
            add_move(moves, from, two, { pawn_double = true })
          end
        end
        for _, file_offset in ipairs({ -1, 1 }) do
          local to = index(file + file_offset, rank + direction)
          local target = to and state.board[to]
          if to and target and side_of(target) ~= side and kind_of(target) ~= "K" then
            add_pawn_move(moves, from, to, promotion_rank, { capture = target })
          elseif to and to == state.en_passant then
            local captured_square = index(file + file_offset, rank)
            if state.board[captured_square] == opposite(side) .. "P" then
              add_move(moves, from, to, {
                capture = opposite(side) .. "P", en_passant_capture = captured_square,
              })
            end
          end
        end
      elseif kind == "N" then
        add_step_moves(state, moves, from, side, KNIGHT_STEPS)
      elseif kind == "B" then
        add_sliding_moves(state, moves, from, side, BISHOP_DIRECTIONS)
      elseif kind == "R" then
        add_sliding_moves(state, moves, from, side, ROOK_DIRECTIONS)
      elseif kind == "Q" then
        add_sliding_moves(state, moves, from, side, BISHOP_DIRECTIONS)
        add_sliding_moves(state, moves, from, side, ROOK_DIRECTIONS)
      elseif kind == "K" then
        add_step_moves(state, moves, from, side, KING_STEPS)
      end
    end
  end
  return moves
end

function Rules.is_attacked(state, square, by_side)
  local file, rank = coordinates(square)
  local pawn_source_rank = rank + (by_side == "w" and -1 or 1)
  for _, offset in ipairs({ -1, 1 }) do
    local source = index(file + offset, pawn_source_rank)
    if source and state.board[source] == by_side .. "P" then return true end
  end
  for _, step in ipairs(KNIGHT_STEPS) do
    local source = index(file + step[1], rank + step[2])
    if source and state.board[source] == by_side .. "N" then return true end
  end
  for _, step in ipairs(KING_STEPS) do
    local source = index(file + step[1], rank + step[2])
    if source and state.board[source] == by_side .. "K" then return true end
  end
  local function attacked_along(directions, first_kind, second_kind)
    for _, direction in ipairs(directions) do
      local distance = 1
      while true do
        local source = index(file + direction[1] * distance, rank + direction[2] * distance)
        if not source then break end
        local piece = state.board[source]
        if piece then
          if side_of(piece) == by_side then
            local kind = kind_of(piece)
            if kind == first_kind or kind == second_kind then return true end
          end
          break
        end
        distance = distance + 1
      end
    end
    return false
  end
  return attacked_along(BISHOP_DIRECTIONS, "B", "Q") or
    attacked_along(ROOK_DIRECTIONS, "R", "Q")
end

function Rules.in_check(state, side)
  local king
  for square = 1, 64 do
    if state.board[square] == side .. "K" then king = square break end
  end
  return not king or Rules.is_attacked(state, king, opposite(side))
end

local function disable_rook_right(castling, square)
  if square == Rules.square("a1") then castling.wq = false
  elseif square == Rules.square("h1") then castling.wk = false
  elseif square == Rules.square("a8") then castling.bq = false
  elseif square == Rules.square("h8") then castling.bk = false end
end

function Rules.apply(state, move, track_history)
  local next_state = Rules.clone(state)
  local piece = assert(next_state.board[move.from], "move has no source piece")
  local side, kind = side_of(piece), kind_of(piece)
  local captured = next_state.board[move.to]
  next_state.board[move.from] = nil
  if move.en_passant_capture then
    captured = next_state.board[move.en_passant_capture]
    next_state.board[move.en_passant_capture] = nil
  end
  next_state.board[move.to] = side .. (move.promotion or kind)
  if move.castle_rook_from then
    next_state.board[move.castle_rook_to] = next_state.board[move.castle_rook_from]
    next_state.board[move.castle_rook_from] = nil
  end
  if kind == "K" then
    next_state.castling[side .. "k"] = false
    next_state.castling[side .. "q"] = false
  elseif kind == "R" then
    disable_rook_right(next_state.castling, move.from)
  end
  if captured and kind_of(captured) == "R" then disable_rook_right(next_state.castling, move.to) end
  next_state.en_passant = nil
  if kind == "P" and math.abs(move.to - move.from) == 16 then
    next_state.en_passant = math.floor((move.to + move.from) / 2)
  end
  next_state.halfmove = (kind == "P" or captured) and 0 or state.halfmove + 1
  if side == "b" then next_state.fullmove = state.fullmove + 1 end
  next_state.turn = opposite(side)
  if track_history ~= false then
    local key = Rules.position_key(next_state)
    next_state.position_counts[key] = (next_state.position_counts[key] or 0) + 1
  end
  return next_state
end

local function add_castles(state, moves, side)
  local rank = side == "w" and 1 or 8
  local king_from = index(5, rank)
  if state.board[king_from] ~= side .. "K" or Rules.in_check(state, side) then return end
  local enemy = opposite(side)
  if state.castling[side .. "k"] and state.board[index(8, rank)] == side .. "R" and
      not state.board[index(6, rank)] and not state.board[index(7, rank)] and
      not Rules.is_attacked(state, index(6, rank), enemy) and
      not Rules.is_attacked(state, index(7, rank), enemy) then
    add_move(moves, king_from, index(7, rank), {
      castle_rook_from = index(8, rank), castle_rook_to = index(6, rank),
    })
  end
  if state.castling[side .. "q"] and state.board[index(1, rank)] == side .. "R" and
      not state.board[index(2, rank)] and not state.board[index(3, rank)] and
      not state.board[index(4, rank)] and
      not Rules.is_attacked(state, index(4, rank), enemy) and
      not Rules.is_attacked(state, index(3, rank), enemy) then
    add_move(moves, king_from, index(3, rank), {
      castle_rook_from = index(1, rank), castle_rook_to = index(4, rank),
    })
  end
end

function Rules.legal_moves(state, from)
  from = from and Rules.square(from) or nil
  local candidates = pseudo_moves(state, state.turn)
  add_castles(state, candidates, state.turn)
  local legal = {}
  for _, move in ipairs(candidates) do
    if (not from or move.from == from) and
        not Rules.in_check(Rules.apply(state, move, false), state.turn) then
      legal[#legal + 1] = move
    end
  end
  return legal
end

function Rules.move_key(move)
  return Rules.square_name(move.from) .. Rules.square_name(move.to) ..
    (move.promotion and move.promotion:lower() or "")
end

function Rules.play(state, from, to, promotion)
  from, to = Rules.square(from), Rules.square(to)
  if not from or not to then return nil, nil, "invalid_square" end
  promotion = promotion and promotion:upper() or nil
  local fallback
  for _, move in ipairs(Rules.legal_moves(state, from)) do
    if move.to == to then
      if move.promotion == promotion then return Rules.apply(state, move), move end
      if not move.promotion then return Rules.apply(state, move), move end
      if move.promotion == "Q" then fallback = move end
    end
  end
  if fallback then return Rules.apply(state, fallback), fallback end
  return nil, nil, "illegal_move"
end

function Rules.draw_reason(state)
  if state.halfmove >= 100 then return "fifty-move rule" end
  if (state.position_counts[Rules.position_key(state)] or 0) >= 3 then
    return "threefold repetition"
  end
  local minor_count, bishop_color, bishops_only = 0, nil, true
  for square = 1, 64 do
    local piece = state.board[square]
    local kind = kind_of(piece)
    if kind and kind ~= "K" then
      if kind == "B" or kind == "N" then
        minor_count = minor_count + 1
        if kind == "B" then
          local file, rank = coordinates(square)
          local color = (file + rank) % 2
          if bishop_color == nil then bishop_color = color
          elseif bishop_color ~= color then bishops_only = false end
        else
          bishops_only = false
        end
      else
        return nil
      end
    end
  end
  if minor_count == 0 or minor_count == 1 or bishops_only then return "insufficient material" end
  return nil
end

function Rules.status(state)
  local legal = Rules.legal_moves(state)
  local checked = Rules.in_check(state, state.turn)
  if #legal == 0 then
    if checked then
      return { kind = "checkmate", winner = opposite(state.turn), in_check = true, legal_moves = legal }
    end
    return { kind = "stalemate", in_check = false, legal_moves = legal }
  end
  local reason = Rules.draw_reason(state)
  if reason then return { kind = "draw", reason = reason, in_check = checked, legal_moves = legal } end
  return { kind = "playing", in_check = checked, legal_moves = legal }
end

function Rules.notation(state, move)
  local piece = state.board[move.from]
  if move.castle_rook_from then return move.to > move.from and "O-O" or "O-O-O" end
  local kind = kind_of(piece)
  local prefix = kind == "P" and "" or kind
  local capture = move.capture and "x" or "-"
  local text = prefix .. Rules.square_name(move.from) .. capture .. Rules.square_name(move.to)
  if move.promotion then text = text .. "=" .. move.promotion end
  local next_state = Rules.apply(state, move, false)
  local status = Rules.status(next_state)
  if status.kind == "checkmate" then text = text .. "#"
  elseif status.in_check then text = text .. "+" end
  return text
end

return Rules
