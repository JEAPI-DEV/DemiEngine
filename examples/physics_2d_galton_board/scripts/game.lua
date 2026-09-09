local Board = require("board")

local Game = {}

-- Enough samples to make the bell curve visible without turning a compact
-- gameplay example into a worst-case contact-stack benchmark.
local BALL_COUNT = 250
local RELEASE_INTERVAL = 0.05
local RELEASE_CLEARANCE = 0.40
local SETTLED_HEIGHT = -5.55

local function ball_id(index)
  return "ball_" .. index
end

local function make_ball(index)
  local horizontal_jitter = Random.range(-0.11, 0.11)
  assert(Entity.create(ball_id(index), {
    components = {
      Transform2D = {
        position = { Board.release_x + horizontal_jitter, Board.release_y },
      },
      Sprite = {
        shape = "circle",
        size = { 0.22, 0.22 },
        color = { 0.42, 0.62, 1.0, 1.0 },
        layer = "balls",
        sorting_order = 5,
      },
      Rigidbody2D = {
        body_type = "dynamic",
        velocity = { horizontal_jitter, 0.0 },
        gravity_scale = 1.0,
        bounciness = 0.02,
        linear_damping = 0.20,
        lock_rotation = false,
        continuous = true,
      },
      CircleCollider2D = {
        radius = 0.11,
        friction = 0.40,
        restitution = 0.02,
        density = 0.7,
        layer = "ball",
        debug_visible = false,
      },
    },
  }))
end

function Game:reset()
  if #self.ball_ids > 0 then Entity.destroy_many(self.ball_ids) end
  self.ball_ids = {}
  self.counted = {}
  self.bins = {}
  for bin = 1, Board.bin_count do self.bins[bin] = 0 end
  self.next_ball = 1
  self.release_elapsed = 0.0
  self.last_released_id = nil
  self.is_releasing = false
  self.release_complete_reported = false
  self.distribution_reported = false
  self.settled_count = 0
  Hud.set_text("status", "Ready - press SPACE to release " .. BALL_COUNT .. " balls")
end

function Game:on_create()
  self.ball_ids = {}
  self.counted = {}
  self.bins = {}
  self:reset()
  Board.create()
end

-- A macroscopic rigid-body simulation cannot represent microscopic launch
-- differences at every pin. A small deterministic perturbation on first peg
-- contact prevents one early deflection from staying correlated for all rows.
-- @OnEvent("physics_collision_enter")
function Game:on_peg_contact(contact)
  if contact.other_layer == "peg" and
    string.sub(contact.entity_id or "", 1, 5) == "ball_" then
    local direction = Random.value() < 0.5 and -1.0 or 1.0
    Rigidbody2D.set_velocity_x(contact.entity_id,
      direction * Random.range(1.35, 1.75))
  end
end

function Game:on_update(dt)
  if Input.action_pressed("reset") then self:reset() end
  if Input.action_pressed("release") and self.next_ball <= BALL_COUNT then
    self.is_releasing = true
    print("Galton board release started")
  end

  if self.is_releasing then
    self.release_elapsed = self.release_elapsed + dt
    local release_clear = true
    if self.last_released_id then
      local _, last_y = Transform.get_position(self.last_released_id)
      release_clear = last_y and last_y <= Board.release_y - RELEASE_CLEARANCE
    end
    if self.release_elapsed >= RELEASE_INTERVAL and release_clear and
      self.next_ball <= BALL_COUNT then
      self.release_elapsed = 0.0
      local id = ball_id(self.next_ball)
      make_ball(self.next_ball)
      self.ball_ids[#self.ball_ids + 1] = id
      self.last_released_id = id
      self.next_ball = self.next_ball + 1
    end
    if self.next_ball > BALL_COUNT then
      self.is_releasing = false
      if not self.release_complete_reported then
        self.release_complete_reported = true
        print("Galton board release complete: " .. BALL_COUNT .. " balls")
      end
    end
  end

  for _, id in ipairs(self.ball_ids) do
    if not self.counted[id] then
      local x, y = Transform.get_position(id)
      if x and y and y <= SETTLED_HEIGHT then
        local bin = Board.bin_for_x(x)
        self.bins[bin] = self.bins[bin] + 1
        assert(Rigidbody2D.set_continuous(id, false))
        assert(Rigidbody2D.set_report_contacts(id, false))
        self.counted[id] = true
        self.settled_count = self.settled_count + 1
      end
    end
  end

  local spawned = self.next_ball - 1
  Hud.set_text("status", string.format(
    "Spawned %d/%d  |  Reached bins %d  |  center bin %d",
    spawned, BALL_COUNT, self.settled_count,
    self.bins[math.ceil(Board.bin_count * 0.5)]))

  if self.settled_count == BALL_COUNT and not self.distribution_reported then
    self.distribution_reported = true
    local center = math.ceil(Board.bin_count * 0.5)
    local center_band = self.bins[center - 1] + self.bins[center] +
      self.bins[center + 1]
    local outer_band = self.bins[1] + self.bins[2] +
      self.bins[Board.bin_count - 1] + self.bins[Board.bin_count]
    local weighted_total = 0
    local expected = { 0.06, 0.73, 4.03, 13.43, 30.21, 48.34, 56.40,
      48.34, 30.21, 13.43, 4.03, 0.73, 0.06 }
    local absolute_error = 0.0
    for bin = 1, Board.bin_count do
      weighted_total = weighted_total + bin * self.bins[bin]
      absolute_error = absolute_error + math.abs(self.bins[bin] - expected[bin])
    end
    local mean_bin = weighted_total / BALL_COUNT
    print("Galton bins: " .. table.concat(self.bins, ","))
    if center_band > outer_band * 2 and math.abs(mean_bin - center) < 0.50 and
      absolute_error < 100.0 then
      print(string.format(
        "Galton distribution valid: center=%d outer=%d mean=%.2f error=%.1f",
        center_band, outer_band, mean_bin, absolute_error))
    else
      print(string.format(
        "Galton distribution invalid: center=%d outer=%d mean=%.2f error=%.1f",
        center_band, outer_band, mean_bin, absolute_error))
    end
  end
end

return Game
