local Demo = {}

local PLAYER = "ent_player"
local MAP = "ent_map"
local GOAL = "ent_goal"
local GATE_COLUMN = 6
local GATE_ROW = 4
local SPEED = 3.5

local function is_player_goal(contact)
  return contact.entity_id == PLAYER and contact.other_entity_id == GOAL
end

function Demo:set_gate(open)
  self.gate_open = open
  Tilemap2D.set_tile(MAP, "walls", GATE_COLUMN, GATE_ROW, open and 0 or 2)
end

function Demo:on_create()
  self.gate_open = true
  self.message = "Move with WASD/arrows. SPACE closes the tile gate."
  self.subscriptions = {
    Events.subscribe("physics_trigger_enter", function(contact)
      if is_player_goal(contact) then self.message = "Trigger ENTER: goal reached!" end
    end),
    Events.subscribe("physics_trigger_stay", function(contact)
      if is_player_goal(contact) then self.message = "Trigger STAY: standing on goal." end
    end),
    Events.subscribe("physics_trigger_exit", function(contact)
      if is_player_goal(contact) then self.message = "Trigger EXIT: left the goal." end
    end),
  }
end

function Demo:on_start()
  self:set_gate(true)
  assert(Tilemap2D.bake_navigation(MAP))
  Navigation2D.set_blocked(4, 4, true) -- authored polygon obstacle
  Navigation2D.set_cost(8, 2, 4.0)

  local spawns = Tilemap2D.objects(MAP, "markers")
  local hits = Physics2D.overlap_box_all(0, 0, 12, 8)
  Sprite2D.set_layer(PLAYER, "actors")
  Sprite2D.set_sorting_order(PLAYER, 20)
  Sprite2D.set_material(PLAYER, "")

  Hud.text("title", "PHASE 4 - PRODUCTION 2D", 24, 24, 1.5, 0.4, 0.95, 1, 1)
  Hud.text("status",
    string.format("%d map marker, %d colliders found", #spawns, #hits),
    24, 58, 1.0, 0.9, 0.9, 0.95, 1)
  Hud.text("path", "", 24, 84, 0.9, 0.7, 0.85, 1, 1)
end

function Demo:on_fixed_update(dt)
  local x = Input.action_value("move_x")
  local y = Input.action_value("move_y")
  local length = math.sqrt(x * x + y * y)
  if length > 1 then x, y = x / length, y / length end
  Rigidbody2D.move_and_slide(PLAYER, x * SPEED * dt, y * SPEED * dt)
end

function Demo:on_update()
  if Input.action_pressed("toggle_gate") then
    self:set_gate(not self.gate_open)
    self.message = self.gate_open and "Gate opened; navigation refreshed."
      or "Gate closed; navigation refreshed."
  end

  local x, y = Transform.get_position(PLAYER)
  local start_x, start_y = Navigation2D.world_to_cell(x, y)
  local goal_x, goal_y = Navigation2D.world_to_cell(4.5, 2.5)
  local path, diagnostic = Navigation2D.path(
    start_x, start_y, goal_x, goal_y, true)
  Hud.set_text("status", self.message)
  Hud.set_text("path", string.format("Path: %s (%d cells) | Gate: %s",
    diagnostic, #path, self.gate_open and "OPEN" or "CLOSED"))
end

function Demo:on_destroy()
  for _, subscription in ipairs(self.subscriptions) do
    Events.unsubscribe(subscription)
  end
end

return Demo
