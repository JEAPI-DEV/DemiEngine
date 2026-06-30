local state = require("game_state")

local Game = {
  points = 0,
  camera_x = 0.0,
  generated_until_x = 8.0,
  next_platform_index = 1,
}

local CAMERA_ID = "ent_camera_main"
local PLAYER_ID = "ent_player"
local PLATFORM_HEIGHT = 0.45
local GENERATE_AHEAD = 32.0

local function platform_width(index)
  return 2.6 + ((index * 37) % 5) * 0.35
end

local function platform_gap(index)
  return 3.2 + ((index * 19) % 4) * 0.45
end

local function platform_y(index)
  local wave = math.sin(index * 1.37) * 1.15
  local step = ((index % 3) - 1) * 0.35
  return -2.8 + wave + step
end

function Game:on_create()
end

function Game:on_start()
  Hud.text("points", "POINTS: " .. tostring(self.points), 24.0, 24.0, 3.0)
  self:generate_platforms_until(GENERATE_AHEAD)
end

function Game:add_points(amount)
  self.points = self.points + amount
  Hud.set_text("points", "POINTS: " .. tostring(self.points))
end

function Game:generate_platforms_until(target_x)
  while self.generated_until_x < target_x do
    local index = self.next_platform_index
    local width = platform_width(index)
    local gap = platform_gap(index)
    local x = self.generated_until_x + gap + width * 0.5
    local y = platform_y(index)
    local id = "platform_" .. tostring(index)

    Entity.create(id, {
      name = "Generated Platform " .. tostring(index),
      components = {
        Transform2D = {
          position = { x, y },
          rotation = 0.0,
          scale = { 1.0, 1.0 },
        },
        Rigidbody2D = {
          body_type = "static",
          velocity = { 0.0, 0.0 },
          gravity_scale = 0.0,
          lock_rotation = true,
        },
        BoxCollider2D = {
          size = { width, PLATFORM_HEIGHT },
          offset = { 0.0, 0.0 },
          is_trigger = false,
        },
      },
    })

    self.generated_until_x = x + width * 0.5
    self.next_platform_index = self.next_platform_index + 1
  end
end

function Game:update_camera(player_x)
  if state.camera_reset_requested then
    self.camera_x = math.max(0.0, state.respawn_x - 1.5)
    state.camera_x = self.camera_x
    state.camera_reset_requested = false
    Entity.set_position(CAMERA_ID, self.camera_x, 0.0)
    return
  end

  local target_x = math.max(0.0, player_x - 1.5)
  if target_x > self.camera_x then
    self.camera_x = self.camera_x + (target_x - self.camera_x) * 0.08
  end

  state.camera_x = self.camera_x
  Entity.set_position(CAMERA_ID, self.camera_x, 0.0)
end

function Game:update_points(player_x)
  local score = math.max(0, math.floor(player_x * 0.5))
  if score ~= self.points then
    self.points = score
    Hud.set_text("points", "POINTS: " .. tostring(self.points))
  end
end

function Game:on_update(dt)
  local player_x, player_y = Entity.get_position(PLAYER_ID)
  if player_x == nil then
    return
  end

  self:update_camera(player_x)
  self:generate_platforms_until(self.camera_x + GENERATE_AHEAD)
  self:update_points(player_x)

  if Input.is_down("p") and not self.p_was_down then
    self:add_points(1)
  end

  self.p_was_down = Input.is_down("p")
end

function Game:on_fixed_update(dt)
end

function Game:on_destroy()
end

return Game
