local Input = require("demi.input")
local Application = require("demi.application")
local Run = require("dungeon.run")
local World = require("dungeon.world")
local View = require("dungeon.presentation")
local Session = require("dungeon.session")
local Menus = require("dungeon.menus")
local Persistence = require("dungeon.persistence")

local Scene = require("demi.scene")
local Floors = require("dungeon.floors")
local Checkpoint = require("dungeon.checkpoint")
local Game = {}

function Game:build_floor()
  if self.world then
    self.world:destroy()
  end
  Session.generation = (Session.generation or 0) + 1
  self.generation = Session.generation
  self.world = World.new(self.generation)
  self.world:build(self.run)
  self.view = View.new(self.world, self.run)
  self.pending = {}
  self.menus = Menus.new()
end

function Game:on_start()
  Application.set_max_fps(60)
  local floor = Floors.index(Scene.active())
  self.run = Session.pending_run or Run.new()
  local transferred = Session.pending_run ~= nil
  Session.pending_run = nil
  if not transferred then
    Run.enter_floor(self.run, floor)
    self.run.phase = floor == 1 and "ready" or "playing"
  end
  self.paused, self.transitioning = false, false
  self:build_floor()
  Session.game = self
  if Session.autosave_pending then
    Session.autosave_pending = nil
    local saved, message = Persistence.write(self.run)
    self.run.message = saved and self.run.floor.subtitle or message
  end
  print("Dungeon ready: floor=" .. floor .. " enemies=" .. Run.remaining(self.run))
end

function Game:transition(run)
  local target = Floors.scene(run.floor.index)
  if target == Scene.active() then
    self.run = run
    self.paused = false
    self:build_floor()
    return true
  end
  if not Scene.load(target) then
    self.run.message = "Scene transition failed: " .. Scene.error()
    return false
  end
  Session.pending_run = run
  self.transitioning = true
  return true
end

function Game:upgrade(choice)
  if self.transitioning or self.run.phase ~= "upgrade" then
    return
  end
  local next_run = assert(Checkpoint.decode(Checkpoint.encode(self.run)))
  if Run.upgrade(next_run, choice) and self:transition(next_run) then
    if self.transitioning then
      Session.autosave_pending = true
    else
      Persistence.write(next_run)
    end
  end
end

function Game:on_update(dt)
  if self.transitioning then
    return
  end
  if Input.pressed("journal") then
    self:toggle_journal()
  end
  if Input.pressed("inventory") then
    self:toggle_inventory()
  end
  if Input.pressed("save") then
    self:save()
  end
  if Input.pressed("restart") then
    self:restart()
  end
  if Input.pressed("pause") then
    self:toggle_pause()
  end
  if Input.pressed("power") then
    self:upgrade("power")
  end
  if Input.pressed("vitality") then
    self:upgrade("vitality")
  end
  -- Modal menus must not queue combat input for the moment they close.
  if self.run.phase == "playing" and not self.paused and not self.menus.mode then
    self.pending.swap_weapon = self.pending.swap_weapon or Input.pressed("next_weapon")
    self.pending.restore_mana = self.pending.restore_mana or Input.pressed("restore_mana")
    self.pending.heal = self.pending.heal or Input.pressed("heal")
    self.pending.dash = self.pending.dash or Input.pressed("dash")
    self.pending.interact = self.pending.interact or Input.pressed("interact")
  else
    self.pending = {}
  end
  self.view:update(self.run, self.paused, dt)
  self.menus:update(self.run)
end

function Game:on_fixed_update(dt)
  if self.transitioning or not self.world:ready() or self.paused or self.menus.mode then
    return
  end
  local action
  if Session.driver then
    action = Session.driver(Run.observe(self.run)) or {}
    if action.upgrade then
      self:upgrade(action.upgrade)
      return
    end
  else
    local aim_x, aim_y = Input.mouse_world_position()
    action = {
      move_x = Input.value("move_x"),
      move_y = Input.value("move_y"),
      attack = Input.down("attack")
        or (Input.mouse_down("left") and not Input.ui_pointer_captured()),
      aim_x = aim_x,
      aim_y = aim_y,
      dash = self.pending.dash,
      interact = self.pending.interact,
      heal = self.pending.heal,
      restore_mana = self.pending.restore_mana,
      swap_weapon = self.pending.swap_weapon,
    }
  end
  self.pending = {}
  Run.step(self.run, action, self.world, dt)
  self.world:sync_gates(self.run)
  self.view:sounds(self.run.events)
  if self.run.interaction == "shop" and not Session.driver then
    self.menus:open_shop()
  end
end

-- @HandleAction("dungeon_start")
function Game:start()
  if self.run.phase == "ready" then
    local run = Run.new()
    if self:transition(run) then
      if self.transitioning then
        Session.autosave_pending = true
      else
        local saved, message = Persistence.write(run)
        run.message = saved and run.floor.subtitle or message
      end
    end
  end
end

-- @HandleAction("dungeon_restart")
function Game:restart()
  self:transition(Run.new())
end

-- @HandleAction("dungeon_resume")
function Game:toggle_pause()
  if self.menus.mode then
    self.menus.mode = nil
    return
  end
  if self.run.phase == "playing" then
    self.paused = not self.paused
    self.pending = {}
  end
end

-- @HandleAction("dungeon_quick_weapon")
function Game:quick_weapon()
  self.pending.swap_weapon = true
end

-- @HandleAction("dungeon_quick_heal")
function Game:quick_heal()
  self.pending.heal = true
end

-- @HandleAction("dungeon_quick_mana")
function Game:quick_mana()
  self.pending.restore_mana = true
end

-- @HandleAction("dungeon_quick_dodge")
function Game:quick_dodge()
  self.pending.dash = true
end

-- @HandleAction("dungeon_interact")
function Game:interact()
  self.pending.interact = true
end

-- @HandleAction("dungeon_journal")
function Game:toggle_journal()
  if self.run.phase == "playing" and not self.paused then
    if self.menus.mode == "journal" then
      self.menus.mode = nil
    else
      self.menus.mode = "journal"
    end
    self.pending = {}
  end
end

-- @HandleAction("dungeon_inventory")
function Game:toggle_inventory()
  if self.run.phase == "playing" and not self.paused then
    self.menus:toggle()
    self.pending = {}
  end
end

-- @HandleAction("dungeon_item_select")
function Game:select_item(event)
  self.menus:select(event)
end

-- @HandleAction("dungeon_item_use")
function Game:use_item()
  self.menus:use(self.run)
end

-- @HandleAction("dungeon_item_drop")
function Game:drop_item()
  self.menus:drop(self.run)
end

-- @HandleAction("dungeon_item_sell")
function Game:sell_item()
  self.menus:sell(self.run)
end

-- @HandleAction("dungeon_buy")
function Game:buy_item(event)
  self.menus:buy(self.run, event)
end

-- @HandleAction("dungeon_save")
function Game:save()
  local saved, message = Persistence.write(self.run)
  self.run.message = message
  return saved
end

-- @HandleAction("dungeon_title")
function Game:title()
  if self:save() then
    self.run.phase = "ready"
    self.paused = false
    self.menus.mode = nil
  end
end

-- @HandleAction("dungeon_continue")
function Game:continue()
  local restored, message = Persistence.read()
  if not restored then
    self.run.message = message
    return
  end
  self:transition(restored)
end

-- @HandleAction("dungeon_power")
function Game:choose_power()
  self:upgrade("power")
end

-- @HandleAction("dungeon_vitality")
function Game:choose_vitality()
  self:upgrade("vitality")
end

function Game:on_destroy()
  Session.game = nil
  if not self.transitioning then
    Session.driver, Session.pending_run = nil, nil
  end
  if self.world then
    self.world:destroy()
  end
end

return Game
