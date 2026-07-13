local InputBuffer = require("demi.input_buffer")
local Commands = require("demi.command_recognizer")

local Fight = {}

local fighters = {
  ent_fighter_a = {
    health = 100, left = "p1_left", right = "p1_right",
    attack = "p1_attack", special = "p1_special", down = "p1_down", facing = 1,
  },
  ent_fighter_b = {
    health = 100, left = "p2_left", right = "p2_right",
    attack = "p2_attack", special = "p2_special", down = "p2_down", facing = -1,
  },
}

local attacks = {
  light = { damage = 9, knockback_x = 0.24, knockback_y = 0.0 },
  special = { damage = 16, knockback_x = 0.42, knockback_y = 0.05 },
}

local function reset_round(self, winner)
  fighters.ent_fighter_a.health = 100
  fighters.ent_fighter_b.health = 100
  Entity.set_position("ent_fighter_a", -2.0, 0.0)
  Entity.set_position("ent_fighter_b", 2.0, 0.0)
  Animation.play("ent_fighter_a", "idle")
  Animation.play("ent_fighter_b", "idle")
  self.message = winner .. " wins - next round"
end

local function draw_hud(self)
  Hud.set_rect("p1_health", 44, 38, 360 * fighters.ent_fighter_a.health / 100, 22)
  Hud.set_rect("p2_health", 556 + 360 * (1 - fighters.ent_fighter_b.health / 100),
    38, 360 * fighters.ent_fighter_b.health / 100, 22)
  Hud.set_text("round_status", self.message)
end

local function attack_if_ready(self, id, fighter)
  if Input.action_pressed(fighter.down) then
    fighter.buffer:push("down", self.clock)
  end
  if Input.action_pressed(fighter.attack) then
    fighter.buffer:push("attack", self.clock)
  end
  if Input.action_pressed(fighter.special) then
    fighter.buffer:push("special", self.clock)
  end
  local state = Animation.state(id)
  if state == "light" or state == "special" or state == "hit" then return true end
  if Commands.match(fighter.buffer.entries, { "down", "attack" }, 0.18) then
    fighter.buffer:clear()
    Animation.play(id, "special")
    return true
  end
  if fighter.buffer:consume("special", self.clock) then
    Animation.play(id, "special")
    return true
  end
  if fighter.buffer:consume("attack", self.clock) then
    Animation.play(id, "light")
    return true
  end
  return false
end

local function update_fighter(self, id, fighter, opponent_id, dt)
  if attack_if_ready(self, id, fighter) then return end
  local move = 0
  if Input.action_down(fighter.left) then move = move - 1 end
  if Input.action_down(fighter.right) then move = move + 1 end
  local x, y = Entity.get_position(id)
  local opponent_x = Entity.get_position(opponent_id)
  fighter.facing = opponent_x >= x and 1 or -1
  Sprite2D.set_flip(id, fighter.facing < 0, false)
  if move ~= 0 then
    x = math.max(-4.2, math.min(4.2, x + move * 2.8 * dt))
    Entity.set_position(id, x, y)
    Animation.play(id, "walk")
  elseif Animation.state(id) == "walk" then
    Animation.play(id, "idle")
  end
end

function Fight:on_create()
  self.clock = 0
  self.message = "FIGHT!  P1: A/D + F/G    P2: arrows + K/L"
  for _, fighter in pairs(fighters) do fighter.buffer = InputBuffer.new(0.22) end
  self.hit_subscription = Events.subscribe("animation_collision", function(overlap)
    local target = fighters[overlap.target_id]
    local source = fighters[overlap.source_id]
    local attack = attacks[overlap.window]
    if not target or not source or not attack then return end
    target.health = math.max(0, target.health - attack.damage)
    Entity.add_position(overlap.target_id,
      attack.knockback_x * source.facing, attack.knockback_y)
    Animation.play(overlap.target_id, "hit")
    if target.health == 0 then
      reset_round(self, overlap.source_id == "ent_fighter_a" and "P1" or "P2")
    end
  end)
end

function Fight:on_start()
  Hud.rect("p1_health_bg", 42, 36, 364, 26, 0.20, 0.05, 0.07, 1)
  Hud.rect("p2_health_bg", 554, 36, 364, 26, 0.20, 0.05, 0.07, 1)
  Hud.rect("p1_health", 44, 38, 360, 22, 0.25, 0.92, 0.55, 1)
  Hud.rect("p2_health", 556, 38, 360, 22, 0.35, 0.65, 1.0, 1)
  Hud.text("round_status", self.message, 250, 82, 2.0, 0.95, 0.95, 1, 1)
end

function Fight:on_update(dt)
  self.clock = self.clock + dt
  update_fighter(self, "ent_fighter_a", fighters.ent_fighter_a, "ent_fighter_b", dt)
  update_fighter(self, "ent_fighter_b", fighters.ent_fighter_b, "ent_fighter_a", dt)
  draw_hud(self)
end

function Fight:on_destroy()
  Events.unsubscribe(self.hit_subscription)
end

return Fight
