local state = require("game_state")
local main_menu = require("main_menu")
local replication = require("network_replication")
local collectibles = require("game.collectibles")
local hud = require("game.hud")
local score = require("game.score")
local world = require("game.world")

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

local function load_level(module_name)
  return require(module_name or "levels.platformer")
end

function Game:on_create()
end

function Game:reset_level_state()
  self.coins = {}
  self.platforms = {}
  self.next_coin_index = 1
  self.score_axis = self.score_axis or "x"
  score.reset(self)
  hud.reset_fps(self)
  state.extra_jumps = 0
  self.level = load_level(self.level_module)
  self.level:reset()
end

function Game:reset_local_score()
  score.reset_local(self)
end

function Game:create_platform(id, name, x, y, width, height)
  world.create_platform(self, id, name, x, y, width, height)
end

function Game:create_coin(x, y)
  return collectibles.create_coin(self, x, y)
end

function Game:on_start()
  if not state.auto_start then
    Runtime.set_physics_enabled(false)
    return
  end

  state.auto_start = false
  self:reset_level_state()
  hud.update_points(self.points)
  hud.update_extra_jumps(false)
  Hud.set_visible("points", false)
  main_menu.apply_settings()
  main_menu.begin_active_level()
  self.level.on_start(self)
  replication.request_claim_once_sync()
end

function Game:on_update(dt)
  hud.update_fps(self, dt)

  if state.score_reset_requested then
    state.score_reset_requested = false
    self:reset_local_score()
  end

  if state.game_over_pending then
    Events.emit("game.game_over", { points = self.points })
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
  score.update_distance(self, player_x, player_y)
  collectibles.collect_near_player(self, player_x, player_y)
  hud.update_extra_jumps(true)
end

function Game:on_fixed_update(dt)
end

function Game:on_destroy()
end

return Game
