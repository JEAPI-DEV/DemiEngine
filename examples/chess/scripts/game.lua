local Rules = require("chess.rules")
local ChessEngine = require("chess.engine")
local View = require("chess.view")

local Game = {}

local function targets_for(state, selected)
  local targets = {}
  if selected then
    for _, move in ipairs(Rules.legal_moves(state, selected)) do targets[move.to] = move end
  end
  return targets
end

function Game:render()
  self.status = Rules.status(self.state)
  self.view:render(self.state, self.selected, targets_for(self.state, self.selected),
    self.last_move, self.status, self.engine_stats, self.history, self.depth, self.ai_pending)
end

function Game:new_game()
  self.state = Rules.new_game()
  self.selected, self.last_move, self.engine_stats = nil, nil, nil
  self.history, self.ai_pending, self.ai_delay = {}, false, 0
  self:render()
end

function Game:set_depth(depth)
  self.depth = depth
  self.engine_stats = nil
  self:render()
end

function Game:on_create()
  self.depth = 2
  self.view = View.new()
end

function Game:on_start()
  Application.set_max_fps(60)
  self.view:build()
  self:new_game()
end

function Game:select_square(square)
  if self.status.kind ~= "playing" or self.state.turn ~= "w" or self.ai_pending then return end
  local piece = self.state.board[square]
  if piece and piece:sub(1, 1) == "w" then
    self.selected = square
    self:render()
    return
  end
  if not self.selected then return end
  local next_state, move = Rules.play(self.state, self.selected, square, "Q")
  if not next_state then
    self.selected = nil
    self:render()
    return
  end
  self.history[#self.history + 1] = Rules.notation(self.state, move)
  self.state, self.last_move, self.selected = next_state, move, nil
  self.status = Rules.status(self.state)
  self.ai_pending = self.status.kind == "playing"
  self.ai_delay = 0.18
  self:render()
end

function Game:play_computer_move()
  local move, stats = ChessEngine.choose_move(self.state, self.depth)
  self.engine_stats = stats
  if move then
    self.history[#self.history + 1] = Rules.notation(self.state, move)
    self.state = Rules.apply(self.state, move)
    self.last_move = move
  end
  self.ai_pending = false
  self:render()
end

function Game:on_update(dt)
  if Input.action_pressed("new_game") then self:new_game() end
  if Input.action_pressed("difficulty_easy") then self:set_depth(1) end
  if Input.action_pressed("difficulty_medium") then self:set_depth(2) end
  if Input.action_pressed("difficulty_hard") then self:set_depth(3) end
  if self.ai_pending then
    self.ai_delay = self.ai_delay - dt
    if self.ai_delay <= 0 then self:play_computer_move() end
  end
end

function Game:on_destroy()
  self.view:destroy()
end

-- @HandleAction("chess_square")
function Game:on_square_action(event)
  local name = event.id and event.id:match("^sq_(%a%d)$")
  local square = name and Rules.square(name)
  if square then self:select_square(square) end
end

-- @HandleAction("new_game")
function Game:on_new_game_action(_event) self:new_game() end

-- @HandleAction("difficulty_easy")
-- @HandleAction("difficulty_medium")
-- @HandleAction("difficulty_hard")
function Game:on_difficulty_action(event)
  local depths = { difficulty_easy = 1, difficulty_medium = 2, difficulty_hard = 3 }
  if depths[event.action] then self:set_depth(depths[event.action]) end
end

return Game
