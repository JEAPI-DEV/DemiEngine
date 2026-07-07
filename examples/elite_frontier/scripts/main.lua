local State = require("core.state")
local SaveGame = require("core.save")
local Controls = require("input.controls")
local Market = require("sim.market")
local Ship = require("sim.ship")
local Wireframe = require("render.wireframe")
local HudView = require("ui.hud")
local Actions = require("ui.actions")

local Main = {}

function Main:on_start()
  State.boot()
  SaveGame.read(State)
  Actions.bind({
    state = State,
    market = Market,
    ship = Ship,
    save = SaveGame,
    hud = HudView,
  })
  Runtime.set_physics_enabled(false)
  HudView.update(State)
end

function Main:on_update(dt)
  local controls = Controls.sample()
  if controls.jump then
    Ship.jump(State)
    SaveGame.write(State)
  end
  if controls.dock then
    Ship.try_dock(State)
    SaveGame.write(State)
  end
  Ship.update(State, controls, dt)
  Wireframe.draw(State, dt)
  HudView.update(State)
end

function Main:on_destroy()
  SaveGame.write(State)
end

return Main
