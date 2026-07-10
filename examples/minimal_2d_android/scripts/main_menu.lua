local state = require("game_state")
local replication = require("network_replication")
local view = require("menu.view")
local settings_module = require("menu.settings")
local network_module = require("menu.network")
local actions = require("menu.actions")

local Menu = {
  screen = "main",
  volume = 1.0,
  network_port = 39420,
  network_level = 1,
  network_join_pending = false,
  network_join_elapsed = 0.0,
  network_join_timeout = 8.0,
  network_lobby_id = nil,
  network_player_token = nil,
  network_heartbeat_elapsed = 0.0,
  network_heartbeat_interval = 12.0,
  key_was_down = {},
  enter_was_down = false,
}

local PLAYER_ID = "ent_player"
local SCENE_MENU = "scene://minimal_2d_android/menu"
local SCENE_PLATFORMER = "scene://minimal_2d_android/platformer"
local SCENE_SPIRAL = "scene://minimal_2d_android/spiral"

local settings = nil
local network = nil

local function scene_for_level(level)
  if level == 2 then
    return SCENE_SPIRAL
  end
  return SCENE_PLATFORMER
end

local function active_scene()
  return scene_for_level(state.level or 1)
end

local function multiplayer_active()
  local session = replication.current_session()
  return session ~= nil and session.scene_id ~= nil
end

local function select_level(scene_id, level)
  state.pending_scene = scene_id
  state.level = level
  state.auto_start = true
  state.fall_floor_y = nil
  state.game_over = false
  state.game_over_pending = false
  Scene.load(scene_id)
end

local function show_options()
  view.show_options(Menu, settings.update_volume_hud)
end

local function pressed_once(key)
  local down = Input.is_down(key)
  local pressed = down and not Menu.key_was_down[key]
  Menu.key_was_down[key] = down
  return pressed
end

function Menu.start_game()
  select_level(SCENE_PLATFORMER, 1)
end

function Menu.apply_settings()
  settings.apply()
end

function Menu.start()
  state.menu_open = true
  state.game_over = false
  state.game_over_pending = false
  Menu.screen = "main"
  Menu.key_was_down = {}
  Menu.enter_was_down = true
  Menu.apply_settings()
  Runtime.set_physics_enabled(false)
  view.set_game_hud_visible(false)
  view.show_group("game_over", false)
  view.show_main(Menu)
end

function Menu.begin_active_level()
  view.hide_menu()
  state.menu_open = false
  state.game_started = true
  state.game_over = false
  state.game_over_pending = false
  Runtime.set_physics_enabled(true)
  view.set_game_hud_visible(true)
  local px, py = Entity.get_position(PLAYER_ID)
  if px ~= nil and py ~= nil then
    state.respawn_x = px
    state.respawn_y = py
  end
  Entity.set_position(PLAYER_ID, state.respawn_x, state.respawn_y)
  Rigidbody2D.set_velocity(PLAYER_ID, 0.0, 0.0)
end

function Menu.show_game_over(points)
  state.menu_open = true
  state.game_started = false
  state.game_over = true
  state.game_over_pending = false
  Runtime.set_physics_enabled(false)
  view.set_game_hud_visible(false)
  view.hide_menu()
  Hud.set_text("game_over_points", "POINTS: " .. tostring(points))
  Hud.set_button_label("game_retry", multiplayer_active() and "RESPAWN" or "TRY AGAIN")
  view.show_group("game_over", true)
  Menu.screen = "game_over"
end

-- @OnEvent("game.game_over")
function Menu.on_game_over(event)
  Menu.show_game_over(event.points or 0)
end

function Menu.retry_game()
  if multiplayer_active() then
    state.menu_open = false
    state.game_started = true
    state.game_over = false
    state.game_over_pending = false
    state.score_reset_requested = true
    state.camera_reset_requested = true
    Runtime.set_physics_enabled(true)
    view.set_game_hud_visible(true)
    view.hide_menu()
    Entity.set_position(PLAYER_ID, state.respawn_x, state.respawn_y)
    Rigidbody2D.set_velocity(PLAYER_ID, 0.0, 0.0)
    return
  end

  state.auto_start = true
  state.game_over = false
  state.game_over_pending = false
  state.score_reset_requested = false
  state.extra_jumps = 0
  Scene.load(active_scene())
end

function Menu.back_to_main_menu()
  if network ~= nil then
    network.disconnect()
  end
  state.auto_start = false
  state.game_over = false
  state.game_over_pending = false
  state.extra_jumps = 0
  state.level = 1
  Scene.load(SCENE_MENU)
end

function Menu.update_network(dt)
  if network ~= nil then
    network.update_lobby(dt)
  end
end

function Menu.update(dt)
  if network ~= nil then
    network.update_lobby(dt)
  end

  local enter_down = Input.is_down("return") or Input.is_down("space")
  local enter_pressed = enter_down and not Menu.enter_was_down

  if Menu.screen == "main" and enter_pressed then
    Menu.start_game()
  end

  if Menu.screen == "levels" then
    if Input.is_down("1") then
      select_level(SCENE_PLATFORMER, 1)
    elseif Input.is_down("2") then
      select_level(SCENE_SPIRAL, 2)
    elseif Input.is_down("escape") then
      view.show_main(Menu)
    end
  end

  if Menu.screen == "network" or Menu.screen == "network_create" or Menu.screen == "network_password" then
    network.update_connection(dt)
    if Input.is_down("escape") then
      network.back_to_main()
    end
  end

  if Menu.screen == "options" then
    if Input.is_down("left") then
      settings.set_volume(Menu.volume - 0.02)
    elseif Input.is_down("right") then
      settings.set_volume(Menu.volume + 0.02)
    end
  end

  Menu.enter_was_down = enter_down
end

settings = settings_module.bind(Menu, view)
network = network_module.bind(Menu, {
  scene_for_level = scene_for_level,
  select_level = select_level,
  show_main = function()
    view.show_main(Menu)
  end,
})
actions.bind({
  menu = Menu,
  view = view,
  settings = settings,
  network = network,
  scenes = {
    platformer = SCENE_PLATFORMER,
    spiral = SCENE_SPIRAL,
  },
  select_level = select_level,
  show_options = show_options,
  retry_game = Menu.retry_game,
  back_to_main_menu = Menu.back_to_main_menu,
})

return Menu
