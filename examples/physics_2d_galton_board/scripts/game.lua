local Board = require("board")

local Game = {}

-- Enough samples to make the bell curve visible without turning a compact
-- gameplay example into a worst-case contact-stack benchmark.
local BALL_COUNT = 250
local RELEASE_INTERVAL = 0.10
local SETTLED_HEIGHT = -5.85

local function ball_id(index)
  return "ball_" .. index
end

local function make_ball(index)
  local horizontal_jitter = Random.range(-0.045, 0.045)
  assert(Entity.create(ball_id(index), {
    components = {
      Transform2D = {
        position = { Board.release_x + horizontal_jitter, Board.release_y },
      },
      Sprite = {
        shape = "circle",
        size = { 0.34, 0.34 },
        color = { 0.42, 0.62, 1.0, 1.0 },
        layer = "balls",
        sorting_order = 5,
      },
      Rigidbody2D = {
        body_type = "dynamic",
        velocity = { horizontal_jitter * 2.0, 0.0 },
        gravity_scale = 1.0,
        bounciness = 0.24,
        lock_rotation = false,
        continuous = true,
      },
      CircleCollider2D = {
        radius = 0.17,
        friction = 0.08,
        restitution = 0.24,
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
  self.is_releasing = false
  self.release_complete_reported = false
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

function Game:on_update(dt)
  if Input.action_pressed("reset") then self:reset() end
  if Input.action_pressed("release") and self.next_ball <= BALL_COUNT then
    self.is_releasing = true
    print("Galton board release started")
  end

  if self.is_releasing then
    self.release_elapsed = self.release_elapsed + dt
    while self.release_elapsed >= RELEASE_INTERVAL and self.next_ball <= BALL_COUNT do
      self.release_elapsed = self.release_elapsed - RELEASE_INTERVAL
      local id = ball_id(self.next_ball)
      make_ball(self.next_ball)
      self.ball_ids[#self.ball_ids + 1] = id
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
end

return Game
