local Game = {}

local BALL_COUNT = 250
local ROW_COUNT = 12
local BIN_COUNT = ROW_COUNT + 1
local BIN_SPACING = 1.05
local RELEASE_Y = 8.8
local BIN_ENTRANCE_Y = -5.45
local RELEASE_INTERVAL = 0.05
local RELEASE_CLEARANCE = 0.30
local BALL_COLORS = {
  { 0.90, 0.94, 0.98, 1.0 },
  { 0.10, 0.42, 0.95, 1.0 },
  { 0.20, 0.78, 0.55, 1.0 },
  { 0.96, 0.50, 0.12, 1.0 },
}

local function ball_id(index)
  return "ball_" .. index
end

local function make_ball(index)
  local jitter_x = Random.range(-0.10, 0.10)
  local jitter_z = Random.range(-0.08, 0.08)
  assert(Entity.create(ball_id(index), {
    components = {
      Transform3D = { position = { jitter_x, RELEASE_Y, 0.10 + jitter_z } },
      MeshRenderer = {
        shape = "sphere",
        size = { 0.30, 0.30, 0.30 },
        color = BALL_COLORS[(index - 1) % #BALL_COLORS + 1],
      },
      Rigidbody3D = {
        body_type = "dynamic",
        velocity = { jitter_x, 0.0, jitter_z },
        mass = 0.7,
        linear_damping = 0.16,
        angular_damping = 0.12,
        friction = 0.40,
        restitution = 0.04,
        continuous = true,
      },
      SphereCollider3D = { radius = 0.15, layer = "ball" },
    },
  }))
end

function Game:reset()
  if #self.ball_ids > 0 then Entity.destroy_many(self.ball_ids) end
  self.ball_ids = {}
  self.counted = {}
  self.bins = {}
  for bin = 1, BIN_COUNT do self.bins[bin] = 0 end
  self.next_ball = 1
  self.release_elapsed = 0.0
  self.last_released_id = nil
  self.is_releasing = false
  self.release_complete_reported = false
  self.distribution_reported = false
  self.settled_count = 0
  Hud.set_text("status", "SPACE: release 250 physical spheres")
end

function Game:on_create()
  self.ball_ids = {}
  self.counted = {}
  self.bins = {}
  self:reset()
end

local function bin_for_x(x)
  local left = -BIN_COUNT * BIN_SPACING * 0.5
  local bin = math.floor((x - left) / BIN_SPACING) + 1
  return math.max(1, math.min(BIN_COUNT, bin))
end

-- @OnEvent("physics3d_collision_enter")
function Game:on_peg_contact(contact)
  if contact.other_layer ~= "peg" or
    string.sub(contact.entity_id or "", 1, 5) ~= "ball_" then return end
  local _, velocity_y, velocity_z = Rigidbody3D.get_velocity(contact.entity_id)
  if not velocity_y then return end
  local direction = Random.value() < 0.5 and -1.0 or 1.0
  Rigidbody3D.set_velocity(contact.entity_id,
    direction * Random.range(1.35, 1.75), velocity_y,
    math.max(-0.45, math.min(0.45, velocity_z + Random.range(-0.12, 0.12))))
end

function Game:on_update(dt)
  if Input.action_pressed("reset") then self:reset() end
  if Input.action_pressed("release") and self.next_ball <= BALL_COUNT then
    self.is_releasing = true
    print("3D Galton board release started")
  end

  if self.is_releasing then
    self.release_elapsed = self.release_elapsed + dt
    local release_clear = true
    if self.last_released_id then
      local _, last_y = Transform3D.get_position(self.last_released_id)
      release_clear = last_y and last_y <= RELEASE_Y - RELEASE_CLEARANCE
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
        print("3D Galton board release complete: " .. BALL_COUNT .. " balls")
      end
    end
  end

  for _, id in ipairs(self.ball_ids) do
    if not self.counted[id] then
      local x, y = Transform3D.get_position(id)
      if x and y and y <= BIN_ENTRANCE_Y then
        local bin = bin_for_x(x)
        self.bins[bin] = self.bins[bin] + 1
        assert(Rigidbody3D.set_continuous(id, false))
        assert(Rigidbody3D.set_report_contacts(id, false))
        self.counted[id] = true
        self.settled_count = self.settled_count + 1
      end
    end
  end

  Hud.set_text("status", string.format(
    "Jolt spheres %d/%d  |  reached bins %d  |  center %d",
    self.next_ball - 1, BALL_COUNT, self.settled_count,
    self.bins[math.ceil(BIN_COUNT * 0.5)]))

  if self.settled_count == BALL_COUNT and not self.distribution_reported then
    self.distribution_reported = true
    local center = math.ceil(BIN_COUNT * 0.5)
    local center_band = self.bins[center - 1] + self.bins[center] +
      self.bins[center + 1]
    local outer_band = self.bins[1] + self.bins[2] +
      self.bins[BIN_COUNT - 1] + self.bins[BIN_COUNT]
    local weighted_total = 0
    for bin = 1, BIN_COUNT do
      weighted_total = weighted_total + bin * self.bins[bin]
    end
    local mean_bin = weighted_total / BALL_COUNT
    print("3D Galton bins: " .. table.concat(self.bins, ","))
    if center_band > outer_band * 2 and math.abs(mean_bin - center) < 0.55 then
      print(string.format(
        "3D Galton distribution valid: center=%d outer=%d mean=%.2f",
        center_band, outer_band, mean_bin))
    else
      print(string.format(
        "3D Galton distribution invalid: center=%d outer=%d mean=%.2f",
        center_band, outer_band, mean_bin))
    end
  end
end

return Game
