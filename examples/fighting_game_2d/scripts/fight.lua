local InputBuffer = require("demi.input_buffer")
local Commands = require("demi.command_recognizer")
local GameplayEvents = require("demi.gameplay.events")
local Health = require("demi.gameplay.health")

local Fight = {}

local fighters = {
  ent_fighter_a = {
    left = "p1_left", right = "p1_right",
    attack = "p1_attack", special = "p1_special", down = "p1_down", facing = 1,
  },
  ent_fighter_b = {
    left = "p2_left", right = "p2_right",
    attack = "p2_attack", special = "p2_special", down = "p2_down", facing = -1,
  },
}

local attacks = {
  light = { damage = 9, knockback_x = 0.24, knockback_y = 0.0 },
  special = { damage = 16, knockback_x = 0.42, knockback_y = 0.05 },
}

local gameplay_events = GameplayEvents.new()
local health = Health.new(gameplay_events)

local function reset_round(self, winner)
  health:remove("ent_fighter_a")
  health:remove("ent_fighter_b")
  health:add("ent_fighter_a", 100)
  health:add("ent_fighter_b", 100)
  Transform.set_position("ent_fighter_a", -2.0, 0.0)
  Transform.set_position("ent_fighter_b", 2.0, 0.0)
  Animation.play("ent_fighter_a", "idle")
  Animation.play("ent_fighter_b", "idle")
  self.message = winner .. " wins - next round"
end

local function draw_hud(self)
  local p1_health = health:get("ent_fighter_a").current
  local p2_health = health:get("ent_fighter_b").current
  Hud.set_rect("p1_health", 44, 38, 360 * p1_health / 100, 22)
  Hud.set_rect("p2_health", 556 + 360 * (1 - p2_health / 100),
    38, 360 * p2_health / 100, 22)
  Hud.set_text("round_status", self.message)
end

local function attack_if_ready(self, id, fighter)
  if Input.pressed(fighter.down) then
    fighter.buffer:push("down", self.clock)
  end
  if Input.pressed(fighter.attack) then
    fighter.buffer:push("attack", self.clock)
  end
  if Input.pressed(fighter.special) then
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
  if Input.down(fighter.left) then move = move - 1 end
  if Input.down(fighter.right) then move = move + 1 end
  local x, y = Transform.get_position(id)
  local opponent_x = Transform.get_position(opponent_id)
  fighter.facing = opponent_x >= x and 1 or -1
  Sprite2D.set_flip(id, fighter.facing < 0, false)
  if move ~= 0 then
    x = math.max(-4.2, math.min(4.2, x + move * 2.8 * dt))
    Transform.set_position(id, x, y)
    Animation.play(id, "walk")
  elseif Animation.state(id) == "walk" then
    Animation.play(id, "idle")
  end
end

function Fight:on_create()
  self.clock = 0
  self.message = "FIGHT!  P1: A/D + F/G    P2: arrows + K/L"
  for _, fighter in pairs(fighters) do fighter.buffer = InputBuffer.new(0.22) end
  health:add("ent_fighter_a", 100)
  health:add("ent_fighter_b", 100)
  self.defeat_subscription = gameplay_events:on("entity_defeated", function(event)
    reset_round(self, event.source == "ent_fighter_a" and "P1" or "P2")
  end)
  self.hit_subscription = Events.subscribe("animation_collision", function(overlap)
    local target = fighters[overlap.target_id]
    local source = fighters[overlap.source_id]
    local attack = attacks[overlap.window]
    if not target or not source or not attack then return end
    health:damage({ source = overlap.source_id, target = overlap.target_id,
      amount = attack.damage, type = "physical", tags = { "melee", overlap.window } })
    Transform.add_position(overlap.target_id,
      attack.knockback_x * source.facing, attack.knockback_y)
    Animation.play(overlap.target_id, "hit")
    gameplay_events:flush()
  end)
end

function Fight:on_start()
  Hud.set_text("round_status", self.message)
end

function Fight:on_update(dt)
  self.clock = self.clock + dt
  update_fighter(self, "ent_fighter_a", fighters.ent_fighter_a, "ent_fighter_b", dt)
  update_fighter(self, "ent_fighter_b", fighters.ent_fighter_b, "ent_fighter_a", dt)
  draw_hud(self)
end

function Fight:on_destroy()
  Events.unsubscribe(self.hit_subscription)
  self.defeat_subscription()
end

return Fight
