local Config = require("game.config")
local State = require("game.state")
local Building = require("game.building")
local Waves = require("game.waves")
local Combat = require("game.combat")
local Persistence = require("game.persistence")
local Ui = require("game.ui")
local Actions = require("game.actions")

local Game = {}

function Game:on_create()
  self.state = State.new(Config)
  self.building = Building.new(self.state, Config)
  self.waves = Waves.new(self.state, Config)
  self.persistence = Persistence.new(self.state, Config, self.building, self.waves)
  Actions.bind({ building = self.building, waves = self.waves })
end

function Game:on_start()
  self.state.status = "Defend the keep. Build towers without blocking the route."
  Ui.update(self.state)
end

local function mouse_tile()
  local world_x, world_y = Input.mouse_world_position()
  local tile_x, tile_y = Grid.world_to_tile(world_x, world_y)
  if not tile_x then return nil end
  return math.floor(tile_x + 0.5), math.floor(tile_y + 0.5)
end

function Game:handle_input()
  if Input.action_pressed("build_arrow") then self.building.select("arrow") end
  if Input.action_pressed("build_cannon") then self.building.select("cannon") end
  if Input.action_pressed("start_wave") then self.waves.start() end
  if Input.action_pressed("cancel") then self.building.cancel() end
  if Input.action_pressed("save") then self.persistence.save() end
  if Input.action_pressed("load") then self.persistence.load() end

  local x, y = mouse_tile()
  if x and self.state.build_kind then self.building.preview(x, y) end

  local left_down = Input.mouse_down("left")
  if left_down and not self.state.left_mouse_was_down and x then
    local mouse_x = Input.mouse_position()
    local viewport_width = Input.viewport_size()
    if mouse_x < viewport_width * 0.72 then
      if self.state.build_kind then
        self.building.place(x, y)
      else
        self.building.select_at(x, y)
      end
    end
  end
  self.state.left_mouse_was_down = left_down
end

function Game:on_update(dt)
  self:handle_input()
  self.waves.update(dt)
  Combat.update(self.state, Config, dt)
  Ui.update(self.state)
end

function Game:on_destroy()
  Grid.clear_preview()
end

return Game
