local Actions = require("shooter.actions")
local Combat = require("shooter.combat")
local Config = require("shooter.config")
local HudView = require("shooter.hud")
local Movement = require("shooter.movement")
local Session = require("shooter.session")

local Game = {
  mode = "menu",
  elapsed = 0,
  join_elapsed = 0,
  join_pending = false,
  mobile_x = 0,
  mobile_y = 0,
  facing_x = 1,
  facing_y = 0,
  fire_requested = false,
}

function Game:on_create()
  self.combat = Combat.new(self)
end

function Game:on_start()
  Actions.bind(self)
  Entity.set_sprite_color(Config.player_entity, 0.2, 0.9, 0.75, 0.2)
  HudView.show_menu("PORT " .. tostring(Config.port))
end

function Game:set_mobile_direction(x, y)
  self.mobile_x = x
  self.mobile_y = y
end

function Game:activate(mode, sender_id)
  self.mode = mode
  self.local_id = sender_id
  self.join_pending = false
  self.mobile_x, self.mobile_y = 0, 0
  self.combat:set_local_player(sender_id)

  local color = Config.color_for(sender_id)
  Entity.set_sprite_color(Config.player_entity, color[1], color[2], color[3], color[4])
  local x, y = Config.spawn_for(sender_id, 0)
  Transform.set_position(Config.player_entity, x, y)

  if mode ~= "practice" then
    Session.register_local(Config.player_entity, sender_id, color)
    NetworkSession.emit("player_join", {color = color}, true)
  end
  HudView.show_match()
end

function Game:host_match()
  if self.mode ~= "menu" then
    return
  end
  if not Session.host() then
    HudView.status("HOST FAILED: " .. tostring(NetworkSession.diagnostics().last_error))
    return
  end
  self:activate("host", "host")
end

function Game:join_match()
  if self.mode ~= "menu" or self.join_pending then
    return
  end
  local address = Hud.get_text("server_address") or "127.0.0.1"
  if address == "" then
    address = "127.0.0.1"
  end
  if not Session.connect(address) then
    HudView.status("JOIN FAILED: " .. tostring(NetworkSession.diagnostics().last_error))
    return
  end
  self.join_pending = true
  self.join_elapsed = 0
  HudView.status("CONNECTING TO " .. address)
end

function Game:practice_match()
  if self.mode == "menu" then
    self:activate("practice", "solo")
  end
end

function Game:leave_match(message)
  if self.mode == "host" or self.mode == "client" or self.join_pending then
    NetworkSession.disconnect()
  end
  self.mode = "menu"
  self.local_id = nil
  self.join_pending = false
  self.mobile_x, self.mobile_y = 0, 0
  self.combat = Combat.new(self)
  Debug.clear_lines()
  Transform.set_position(Config.player_entity, 0, 0)
  Entity.set_sprite_color(Config.player_entity, 0.2, 0.9, 0.75, 0.2)
  HudView.show_menu(message or ("PORT " .. tostring(Config.port)))
end

function Game:process_network()
  if self.mode == "practice" or self.mode == "menu" and not self.join_pending then
    return
  end
  local update = NetworkSession.process_events()
  if self.join_pending then
    self.join_elapsed = self.join_elapsed + self.frame_dt
    local diagnostics = NetworkSession.diagnostics()
    if diagnostics.connected and diagnostics.local_peer_id ~= "client" then
      self:activate("client", diagnostics.local_peer_id)
    elseif update.disconnected or self.join_elapsed >= Config.join_timeout then
      self:leave_match("CONNECTION FAILED")
    else
      HudView.status("CONNECTING... " .. string.format("%.1f", self.join_elapsed))
    end
  end
  for _, event in ipairs(update.events) do
    self.combat:on_event(event)
  end
  if update.disconnected and self.mode == "client" then
    self:leave_match("HOST DISCONNECTED")
  end
end

function Game:on_update(dt)
  self.frame_dt = dt
  self.elapsed = self.elapsed + dt
  self:process_network()

  if self.mode == "menu" then
    if Input.action_pressed("host_match") then
      self:host_match()
    elseif Input.action_pressed("join_match") then
      self:join_match()
    elseif Input.action_pressed("practice_match") then
      self:practice_match()
    end
    return
  end

  if Input.action_pressed("leave_match") then
    self:leave_match()
    return
  end

  Movement.update(self, dt)
  if self.mode ~= "practice" then
    Session.update_local(self.local_id, dt)
  end

  local mouse_fire = Input.mouse_down("left") and not Input.ui_pointer_captured()
  if Input.action_pressed("fire") or mouse_fire or self.fire_requested then
    self.fire_requested = false
    self.combat:request_shot()
  end
  self.combat:update_tracer()
  HudView.update(self)
end

function Game:on_destroy()
  if self.mode == "host" or self.mode == "client" then
    NetworkSession.disconnect()
  end
  Debug.clear_lines()
end

return Game
