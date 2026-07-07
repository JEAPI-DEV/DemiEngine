local config = require("player_config")
local state = require("game_state")
local hud = require("game.hud")

local Collectibles = {}

local PLAYER_ID = "ent_player"
local COIN_COLLECT_RADIUS = 0.75
local MIN_COIN_DISTANCE = 2.15
local COIN_PLATFORM_CLEARANCE = 0.8

local function coin_inside_platform(game, x, y)
  for _, p in pairs(game.platforms) do
    local half_w = p.width * 0.5
    local top = p.y + p.height * 0.5
    if x >= p.x - half_w - 0.15 and x <= p.x + half_w + 0.15 and y < top + COIN_PLATFORM_CLEARANCE then
      return true
    end
  end
  return false
end

function Collectibles.create_coin(game, x, y)
  if coin_inside_platform(game, x, y) then
    return false
  end

  local min_distance_squared = MIN_COIN_DISTANCE * MIN_COIN_DISTANCE
  for _, coin in pairs(game.coins) do
    if not coin.collected then
      local dx = x - coin.x
      local dy = y - coin.y
      if (dx * dx) + (dy * dy) < min_distance_squared then
        return false
      end
    end
  end

  local id = "coin_" .. tostring(game.next_coin_index)
  game.next_coin_index = game.next_coin_index + 1

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

  game.coins[id] = { x = x, y = y, collected = false }
  return true
end

function Collectibles.collect_near_player(game, player_x, player_y)
  if (state.extra_jumps or 0) >= config.max_extra_jumps then
    return
  end

  local radius_squared = COIN_COLLECT_RADIUS * COIN_COLLECT_RADIUS
  for id, coin in pairs(game.coins) do
    if not coin.collected then
      local dx = player_x - coin.x
      local dy = player_y - coin.y
      if (dx * dx) + (dy * dy) <= radius_squared then
        coin.collected = true
        Entity.destroy(id)
        Audio.play("asset://audio/collect_coin")
        state.extra_jumps = math.min((state.extra_jumps or 0) + 1, config.max_extra_jumps)
        hud.update_extra_jumps(true)
        if state.extra_jumps >= config.max_extra_jumps then
          return
        end
      end
    end
  end
end

return Collectibles
