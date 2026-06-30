local state = require("game_state")
local main_menu = require("main_menu")

local Game = {
  points = 0,
  camera_x = 0.0,
  generated_until_x = 8.0,
  next_platform_index = 1,
  next_coin_index = 1,
  last_platform_y = -3.0,
  coins = {},
}

local CAMERA_ID = "ent_camera_main"
local PLAYER_ID = "ent_player"
local PLATFORM_HEIGHT = 0.45
local GENERATE_AHEAD = 32.0
local COIN_COLLECT_RADIUS = 0.75
local MIN_COIN_DISTANCE = 2.15

local function pseudo_random(index, salt)
  local value = math.sin(index * 12.9898 + salt * 78.233) * 43758.5453
  return value - math.floor(value)
end

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
  Hud.set_text("points", "POINTS: " .. tostring(self.points))
  Hud.set_visible("points", false)
  self:generate_platforms_until(GENERATE_AHEAD)
  main_menu.start()
end

function Game:add_points(amount)
  self.points = self.points + amount
  Hud.set_text("points", "POINTS: " .. tostring(self.points))
end

function Game:create_coin(x, y)
  local min_distance_squared = MIN_COIN_DISTANCE * MIN_COIN_DISTANCE
  for _, coin in pairs(self.coins) do
    if not coin.collected then
      local dx = x - coin.x
      local dy = y - coin.y
      if (dx * dx) + (dy * dy) < min_distance_squared then
        return false
      end
    end
  end

  local id = "coin_" .. tostring(self.next_coin_index)
  self.next_coin_index = self.next_coin_index + 1

  Entity.create(id, {
    name = "Gold Coin",
    components = {
      Transform2D = {
        position = { x, y },
        rotation = 0.0,
        scale = { 1.0, 1.0 },
      },
      Sprite = {
        texture = "asset://items/gold_coin",
        layer = "collectibles",
      },
    },
  })

  self.coins[id] = { x = x, y = y, collected = false }
  return true
end

function Game:create_gap_coins(index, start_x, end_x, from_y, to_y)
  if index % 2 ~= 0 then
    return
  end

  local gap = end_x - start_x
  if gap < 2.2 then
    return
  end

  local count = index % 4 == 0 and 2 or 1
  for coin = 1, count do
    local t = count == 1 and 0.5 or (0.22 + (coin - 1) * 0.56)
    local x = start_x + gap * t
    local base_y = from_y + (to_y - from_y) * t
    local arc = math.sin(t * math.pi) * (1.8 + pseudo_random(index, coin + 11) * 1.15)
    if count == 2 then
      arc = arc + (coin - 1) * 0.65
    end
    self:create_coin(x, base_y + arc)
  end
end

function Game:create_platform_coin(index, platform_x, platform_y)
  if index % 5 ~= 0 then
    return
  end

  local high_offset = 1.75 + pseudo_random(index, 23) * 1.25
  if not self:create_coin(platform_x, platform_y + high_offset) then
    self:create_coin(platform_x + 1.9, platform_y + high_offset + 0.75)
  end
end

function Game:generate_platforms_until(target_x)
  while self.generated_until_x < target_x do
    local index = self.next_platform_index
    local width = platform_width(index)
    local gap = platform_gap(index)
    local x = self.generated_until_x + gap + width * 0.5
    local y = platform_y(index)
    local id = "platform_" .. tostring(index)
    local previous_platform_edge = self.generated_until_x
    local platform_left_edge = x - width * 0.5

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

    self:create_gap_coins(index, previous_platform_edge, platform_left_edge, self.last_platform_y, y)
    self:create_platform_coin(index, x, y)

    self.generated_until_x = x + width * 0.5
    self.last_platform_y = y
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

function Game:collect_coins(player_x, player_y)
  local radius_squared = COIN_COLLECT_RADIUS * COIN_COLLECT_RADIUS
  for id, coin in pairs(self.coins) do
    if not coin.collected then
      local dx = player_x - coin.x
      local dy = player_y - coin.y
      if (dx * dx) + (dy * dy) <= radius_squared then
        coin.collected = true
        Entity.destroy(id)
        Audio.play("asset://audio/collect_coin")
        self:add_points(1)
      end
    end
  end
end

function Game:on_update(dt)
  if state.menu_open then
    main_menu.update(dt)
    return
  end

  local player_x, player_y = Entity.get_position(PLAYER_ID)
  if player_x == nil or player_y == nil then
    return
  end

  self:update_camera(player_x)
  self:generate_platforms_until(self.camera_x + GENERATE_AHEAD)
  self:collect_coins(player_x, player_y)
end

function Game:on_fixed_update(dt)
end

function Game:on_destroy()
end

return Game
