local Rules = require("chess.rules")

local View = {}
View.__index = View

local PIECES = {
  wP = "asset://chess/w_pawn", wN = "asset://chess/w_knight",
  wB = "asset://chess/w_bishop", wR = "asset://chess/w_rook",
  wQ = "asset://chess/w_queen", wK = "asset://chess/w_king",
  bP = "asset://chess/b_pawn", bN = "asset://chess/b_knight",
  bB = "asset://chess/b_bishop", bR = "asset://chess/b_rook",
  bQ = "asset://chess/b_queen", bK = "asset://chess/b_king",
}
local LIGHT = { 0.79, 0.75, 0.65, 1.0 }
local DARK = { 0.31, 0.37, 0.34, 1.0 }
local SELECTED = { 0.32, 0.53, 0.55, 1.0 }
local TARGET = { 0.42, 0.56, 0.39, 1.0 }
local LAST_MOVE = { 0.64, 0.49, 0.27, 1.0 }
local CHECK = { 0.67, 0.25, 0.22, 1.0 }
local INACTIVE = { 0.11, 0.13, 0.14, 1.0 }
local ACTIVE = { 0.28, 0.37, 0.34, 1.0 }
local ROBOTO = "asset://chess/roboto"

local function set_background(id, color)
  Hud.set_background_color(id, color[1], color[2], color[3], color[4])
end

local function square_id(square) return "sq_" .. Rules.square_name(square) end
local function piece_id(square) return "piece_" .. Rules.square_name(square) end
local function marker_id(square) return "marker_" .. Rules.square_name(square) end

local function checked_king(state, status)
  if not status.in_check then return nil end
  for square = 1, 64 do
    if state.board[square] == state.turn .. "K" then return square end
  end
  return nil
end

function View.new()
  return setmetatable({ built = false }, View)
end

function View:build()
  if self.built then return end
  Hud.clear_children("board_panel")
  local size = 76
  for square = 1, 64 do
    local file = ((square - 1) % 8) + 1
    local rank = math.floor((square - 1) / 8) + 1
    local name = Rules.square_name(square)
    local button, button_error = Hud.create("board_panel", {
      id = square_id(square), type = "button", action = "chess_square",
      accessibility_label = "Chess square " .. name, focusable = true,
      x = (file - 1) * size, y = (8 - rank) * size, width = size, height = size,
    })
    assert(button, button_error)
    local marker, marker_error = Hud.create(square_id(square), {
      id = marker_id(square), type = "rect", accessibility_hidden = true,
      focusable = false, visible = false, x = 31, y = 31, width = 14, height = 14,
    })
    assert(marker, marker_error)
    set_background(marker_id(square), { 0.91, 0.84, 0.62, 0.82 })
    local image, image_error = Hud.create(square_id(square), {
      id = piece_id(square), type = "image", accessibility_hidden = true,
      focusable = false, x = 4, y = 4, width = 68, height = 68,
    })
    assert(image, image_error)
    if file == 1 or rank == 1 then
      local coordinate, coordinate_error = Hud.create(square_id(square), {
        id = "coordinate_" .. name, type = "label", font = ROBOTO,
        text = file == 1 and rank == 1 and "a1" or
          (file == 1 and tostring(rank) or name:sub(1, 1)),
        accessibility_hidden = true, focusable = false,
        x = 4, y = 57, width = 18, height = 15, font_size = 11,
      })
      assert(coordinate, coordinate_error)
      Hud.set_color("coordinate_" .. name, 0.10, 0.12, 0.12, 0.70)
    end
    set_background(square_id(square), (file + rank) % 2 == 0 and DARK or LIGHT)
  end
  self.built = true
end

local function move_lines(history)
  if #history == 0 then return "No moves yet" end
  local first = math.max(1, #history - 9)
  local lines = {}
  for ply = first, #history, 2 do
    local number = math.floor((ply + 1) / 2)
    local line = tostring(number) .. ".  " .. history[ply]
    if history[ply + 1] then line = line .. "    " .. history[ply + 1] end
    lines[#lines + 1] = line
  end
  return table.concat(lines, "\n")
end

local function status_copy(state, status, thinking)
  if status.kind == "checkmate" then
    return status.winner == "w" and "CHECKMATE · YOU WIN" or "CHECKMATE · ENGINE WINS",
      "Game over"
  elseif status.kind == "stalemate" then
    return "STALEMATE", "Game drawn"
  elseif status.kind == "draw" then
    return "DRAW · " .. string.upper(status.reason), "Game over"
  elseif thinking then
    return "ENGINE THINKING", "Black to move"
  elseif state.turn == "w" then
    return status.in_check and "YOUR KING IS IN CHECK" or "YOUR TURN",
      status.in_check and "White must answer the check" or "White to move"
  end
  return "ENGINE TURN", "Black to move"
end

local function castling_copy(state, status)
  local available = {}
  if state.turn == "w" and status.kind == "playing" then
    for _, move in ipairs(status.legal_moves) do
      if move.castle_rook_from then
        available[#available + 1] = move.to > move.from and "King → g1  (O-O)" or
          "King → c1  (O-O-O)"
      end
    end
  end
  if #available > 0 then return "AVAILABLE NOW · " .. table.concat(available, "  /  ") end
  if not state.castling.wk and not state.castling.wq then
    return "White's castling rights are no longer available"
  end
  if state.turn ~= "w" then return "Available options will show on your turn" end
  return "Clear the path, then move King to g1 or c1"
end

function View:render(state, selected, targets, last_move, status, engine_stats,
    history, depth, thinking)
  local king_in_check = checked_king(state, status)
  for square = 1, 64 do
    local file = ((square - 1) % 8) + 1
    local rank = math.floor((square - 1) / 8) + 1
    local color = (file + rank) % 2 == 0 and DARK or LIGHT
    if last_move and (square == last_move.from or square == last_move.to) then color = LAST_MOVE end
    if targets[square] then color = TARGET end
    if square == selected then color = SELECTED end
    if square == king_in_check then color = CHECK end
    set_background(square_id(square), color)
    local piece = state.board[square]
    Hud.set_visible(marker_id(square), targets[square] ~= nil and piece == nil)
    Hud.set_visible(piece_id(square), piece ~= nil)
    if piece then Hud.set_image(piece_id(square), PIECES[piece], 0, 0, 128, 128) end
  end

  local turn_text, status_text = status_copy(state, status, thinking)
  Hud.set_text("turn_label", turn_text)
  Hud.set_text("status_label", status_text)
  Hud.set_text("moves_label", move_lines(history))
  Hud.set_text("castle_label", castling_copy(state, status))
  if engine_stats then
    Hud.set_text("engine_label", string.format(
      "Depth %d · %d nodes · score %d",
      engine_stats.depth, engine_stats.nodes, engine_stats.score))
  else
    Hud.set_text("engine_label", "Engine ready · depth " .. tostring(depth))
  end
  set_background("difficulty_easy_button", depth == 1 and ACTIVE or INACTIVE)
  set_background("difficulty_medium_button", depth == 2 and ACTIVE or INACTIVE)
  set_background("difficulty_hard_button", depth == 3 and ACTIVE or INACTIVE)
end

function View:destroy()
  if self.built then Hud.clear_children("board_panel") end
  self.built = false
end

return View
