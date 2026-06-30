local state = require("game_state")
local main_menu = require("main_menu")
local level_main = require("level_main")
local level_spiral = require("level_spiral")

local Game = {
  points = 0,
  next_coin_index = 1,
  coins = {},
  platforms = {},
  level = nil,
}

local PLAYER_ID = "ent_player"
local COIN_COLLECT_RADIUS = 0.75
local MIN_COIN_DISTANCE = 2.15
local COIN_PLATFORM_CLEARANCE = 0.8

local function active_level()
  return state.level == 2 and level_spiral or level_main
end

function Game:on_create()
end

function Game:reset_level_state()
  self.coins = {}
  self.platforms = {}
  self.points = 0
  self.next_coin_index = 1
  self.level = active_level()
  self.level:reset()
end

function Game:on_start()
  self:reset_level_state()
  Hud.set_text("points", "POINTS: " .. tostring(self.points))
  Hud.set_visible("points", false)
  if state.auto_start then
    state.auto_start = false
    main_menu.apply_settings()
    main_menu.begin_active_level()
    self.level.on_start(self)
  else
    self.level.on_start(self)
    main_menu.start()
  end
end

function Game:add_points(amount)
  self.points = self.points + amount
  Hud.set_text("points", "POINTS: " .. tostring(self.points))
end

function Game:create_platform(id, name, x, y, width, height)
  Entity.create(id, {
    name = name,
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
        size = { width, height },
        offset = { 0.0, 0.0 },
        is_trigger = false,
      },
    },
  })

  self.platforms[id] = { x = x, y = y, width = width, height = height }
end

local function coin_inside_platform(self, x, y)
  for _, p in pairs(self.platforms) do
    local half_w = p.width * 0.5
    local top = p.y + p.height * 0.5
    if x >= p.x - half_w - 0.15 and x <= p.x + half_w + 0.15 and y < top + COIN_PLATFORM_CLEARANCE then
      return true
    end
  end
  return false
end

function Game:create_coin(x, y)
  if coin_inside_platform(self, x, y) then
    return false
  end

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

  self.level.update_camera(self, player_x, player_y)
  self.level.generate_ahead(self)
  self.level.on_update_track(self, player_x, player_y)
  self:collect_coins(player_x, player_y)
end

function Game:on_fixed_update(dt)
end

function Game:on_destroy()
end

return Game
