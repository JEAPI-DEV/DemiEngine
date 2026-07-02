local state = require("game_state")
local config = require("player_config")
local main_menu = require("main_menu")
local level_main = require("level_main")
local level_spiral = require("level_spiral")
local replication = require("network_replication")

local Game = {
  points = 0,
  next_coin_index = 1,
  coins = {},
  platforms = {},
  level = nil,
  score_origin_x = nil,
  score_origin_y = nil,
  best_distance = 0.0,
  fps_accumulator = 0.0,
  fps_frames = 0,
}

local PLAYER_ID = "ent_player"
local COIN_COLLECT_RADIUS = 0.75
local MIN_COIN_DISTANCE = 2.15
local COIN_PLATFORM_CLEARANCE = 0.8
local EXTRA_JUMP_SLOT_IDS = { "extra_jump_slot_1", "extra_jump_slot_2", "extra_jump_slot_3" }
local EXTRA_JUMP_COIN_IDS = { "extra_jump_coin_1", "extra_jump_coin_2", "extra_jump_coin_3" }

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
  self.score_origin_x = nil
  self.score_origin_y = nil
  self.best_distance = 0.0
  self.fps_accumulator = 0.0
  self.fps_frames = 0
  state.extra_jumps = 0
  self.level = active_level()
  self.level:reset()
end

function Game:update_points_hud()
  Hud.set_text("points", "POINTS: " .. tostring(self.points))
end

function Game:update_extra_jump_hud(visible)
  local extra_jumps = state.extra_jumps or 0
  for i = 1, config.max_extra_jumps do
    Hud.set_visible(EXTRA_JUMP_SLOT_IDS[i], visible)
    Hud.set_visible(EXTRA_JUMP_COIN_IDS[i], visible and i <= extra_jumps)
  end
end

function Game:update_fps_hud(dt)
  self.fps_accumulator = self.fps_accumulator + dt
  self.fps_frames = self.fps_frames + 1
  if self.fps_accumulator < 0.25 then
    return
  end

  local fps = math.floor((self.fps_frames / self.fps_accumulator) + 0.5)
  Hud.set_text("fps", "FPS: " .. tostring(fps))
  self.fps_accumulator = 0.0
  self.fps_frames = 0
end

function Game:on_start()
  if not state.auto_start then
    Runtime.set_physics_enabled(false)
    return
  end

  state.auto_start = false
  self:reset_level_state()
  self:update_points_hud()
  self:update_extra_jump_hud(false)
  Hud.set_visible("points", false)
  main_menu.apply_settings()
  main_menu.begin_active_level()
  self.level.on_start(self)
  replication.request_claim_once_sync()
end

function Game:update_distance_score(player_x, player_y)
  if self.score_origin_x == nil or self.score_origin_y == nil then
    self.score_origin_x = player_x
    self.score_origin_y = player_y
  end

  local distance = player_x - self.score_origin_x
  if state.level == 2 then
    distance = player_y - self.score_origin_y
  end

  self.best_distance = math.max(self.best_distance, distance, 0.0)
  local points = math.floor(self.best_distance)
  if points ~= self.points then
    self.points = points
    self:update_points_hud()
  end
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
        layer = "platform",
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
  replication.register_claim_once(id, {
    can_claim = function(object_id, collector_id, claim)
      local coin = self.coins[object_id]
      if coin == nil or coin.collected then
        return false
      end

      local collector_x, collector_y = nil, nil
      if claim ~= nil and claim.x ~= nil and claim.y ~= nil then
        collector_x = claim.x
        collector_y = claim.y
      elseif collector_id == replication.sender_id() then
        collector_x, collector_y = Entity.get_position(PLAYER_ID)
      else
        collector_x, collector_y = replication.remote_position(collector_id)
      end
      if collector_x == nil or collector_y == nil then
        return false
      end

      local dx = collector_x - coin.x
      local dy = collector_y - coin.y
      return (dx * dx) + (dy * dy) <= COIN_COLLECT_RADIUS * COIN_COLLECT_RADIUS
    end,
    on_removed = function(object_id)
      local coin = self.coins[object_id]
      if coin ~= nil then
        coin.collected = true
      end
      Entity.destroy(object_id)
    end,
    on_claimed_local = function()
      Audio.play("asset://audio/collect_coin")
      state.extra_jumps = math.min((state.extra_jumps or 0) + 1, config.max_extra_jumps)
      self:update_extra_jump_hud(true)
    end,
  })
  return true
end

function Game:collect_coins(player_x, player_y)
  if (state.extra_jumps or 0) >= config.max_extra_jumps then
    return
  end

  local radius_squared = COIN_COLLECT_RADIUS * COIN_COLLECT_RADIUS
  for id, coin in pairs(self.coins) do
    if not coin.collected then
      local dx = player_x - coin.x
      local dy = player_y - coin.y
      if (dx * dx) + (dy * dy) <= radius_squared then
        replication.try_claim_once(id, { x = player_x, y = player_y })
        if state.extra_jumps >= config.max_extra_jumps then
          return
        end
      end
    end
  end
end

function Game:on_update(dt)
  self:update_fps_hud(dt)

  if state.game_over_pending then
    main_menu.show_game_over(self.points)
    return
  end

  if state.menu_open or state.game_over or self.level == nil then
    return
  end

  local player_x, player_y = Entity.get_position(PLAYER_ID)
  if player_x == nil or player_y == nil then
    return
  end

  self.level.update_camera(self, player_x, player_y)
  self.level.generate_ahead(self)
  self.level.on_update_track(self, player_x, player_y)
  self:update_distance_score(player_x, player_y)
  self:collect_coins(player_x, player_y)
  self:update_extra_jump_hud(true)
end

function Game:on_fixed_update(dt)
end

function Game:on_destroy()
end

return Game
