local state = require("game_state")

local Menu = {
  mouse_was_down = false,
  enter_was_down = false,
  ids = {},
}

local PLAYER_ID = "ent_player"

local buttons = {
  { id = "start", label = "START GAME" },
  { id = "quit", label = "QUIT" },
}

local function remember(id)
  if Menu.ids[id] then
    return id
  end
  Menu.ids[id] = true
  return id
end

local function text_width(text, scale)
  return #text * scale * 4.0
end

local function contains(rect, x, y)
  return x >= rect.x and x <= rect.x + rect.w and y >= rect.y and y <= rect.y + rect.h
end

local function hide_menu()
  for id in pairs(Menu.ids) do
    Hud.set_visible(id, false)
  end
end

local function start_game()
  hide_menu()
  state.menu_open = false
  state.game_started = true
  Runtime.set_physics_enabled(true)
  Hud.set_visible("points", true)
  Entity.set_position(PLAYER_ID, state.respawn_x, state.respawn_y)
  Rigidbody2D.set_velocity(PLAYER_ID, 0.0, 0.0)
end

local function draw_background(width, height)
  Hud.rect(remember("menu_sky"), 0.0, 0.0, width, height, 0.02, 0.015, 0.09, 1.0)
  Hud.rect(remember("menu_sky_band"), 0.0, height * 0.18, width, height * 0.28, 0.10, 0.07, 0.24, 0.78)
  Hud.rect(remember("menu_shade"), 0.0, height * 0.56, width, height * 0.44, 0.03, 0.02, 0.13, 0.86)

  local star_count = 48
  for i = 1, star_count do
    local x = ((i * 83) % 997) / 997.0 * width
    local y = ((i * 149) % 541) / 541.0 * height * 0.86
    local size = (i % 5 == 0) and 3.0 or 2.0
    local glow = (i % 3 == 0) and 0.82 or 0.58
    Hud.rect(remember("menu_star_" .. tostring(i)), x, y, size, size, 0.72, 0.88, 1.0, glow)
  end

  Hud.rect(remember("menu_nebula_1"), width * 0.06, height * 0.20, width * 0.34, 28.0, 0.94, 0.24, 0.56, 0.28)
  Hud.rect(remember("menu_nebula_2"), width * 0.18, height * 0.25, width * 0.44, 22.0, 0.37, 0.85, 1.0, 0.22)
  Hud.rect(remember("menu_nebula_3"), width * 0.52, height * 0.15, width * 0.32, 30.0, 0.98, 0.70, 0.28, 0.18)
  Hud.rect(remember("menu_horizon"), 0.0, height * 0.74, width, 5.0, 0.38, 0.92, 1.0, 0.48)

  local moon_x = width - 126.0
  Hud.rect(remember("menu_sun"), moon_x, 48.0, 58.0, 58.0, 0.88, 0.95, 1.0, 1.0)
  Hud.rect(remember("menu_moon_cut"), moon_x + 14.0, 42.0, 54.0, 68.0, 0.02, 0.015, 0.09, 1.0)

  Hud.rect(remember("menu_panel_shadow"), width * 0.5 - 258.0, height * 0.31 + 10.0, 516.0, 250.0, 0.37, 0.20, 0.66, 0.20)
  Hud.rect(remember("menu_panel"), width * 0.5 - 260.0, height * 0.31, 520.0, 250.0, 0.08, 0.07, 0.18, 0.64)
  Hud.rect(remember("menu_panel_glow"), width * 0.5 - 252.0, height * 0.31 + 7.0, 504.0, 4.0, 0.49, 0.93, 1.0, 0.54)
end

local function draw_title(width)
  local title = "DEMI ORBIT"
  local subtitle = "COSMIC SLINGSHOT RUNNER"
  local title_scale = 9.0
  local subtitle_scale = 3.0
  local x = width * 0.5 - text_width(title, title_scale) * 0.5
  Hud.text(remember("menu_title_shadow"), title, x + 5.0, 76.0, title_scale, 0.29, 0.12, 0.56, 0.82)
  Hud.text(remember("menu_title"), title, x, 70.0, title_scale, 0.86, 0.96, 1.0, 1.0)
  Hud.text(remember("menu_subtitle"), subtitle, width * 0.5 - text_width(subtitle, subtitle_scale) * 0.5, 150.0, subtitle_scale, 1.0, 0.70, 0.36, 1.0)
end

local function draw_button(button, rect, hovered)
  local base_r = hovered and 0.24 or 0.15
  local base_g = hovered and 0.30 or 0.18
  local base_b = hovered and 0.48 or 0.34
  local beam_a = hovered and 0.72 or 0.40
  Hud.rect(remember("menu_button_shadow_" .. button.id), rect.x + 7.0, rect.y + 7.0, rect.w, rect.h, 0.16, 0.08, 0.35, 0.26)
  Hud.rect(remember("menu_button_border_" .. button.id), rect.x - 2.0, rect.y - 2.0, rect.w + 4.0, rect.h + 4.0, 0.63, 0.91, 1.0, 0.34)
  Hud.rect(remember("menu_button_" .. button.id), rect.x, rect.y, rect.w, rect.h, base_r, base_g, base_b, 0.92)
  Hud.rect(remember("menu_button_top_" .. button.id), rect.x + 6.0, rect.y + 5.0, rect.w - 12.0, 6.0, 0.92, 0.71, 1.0, beam_a)
  Hud.rect(remember("menu_button_glow_" .. button.id), rect.x + 14.0, rect.y + rect.h - 9.0, rect.w - 28.0, 3.0, 0.32, 0.92, 1.0, beam_a)

  local scale = 4.0
  local text_x = rect.x + rect.w * 0.5 - text_width(button.label, scale) * 0.5
  local text_y = rect.y + rect.h * 0.5 - 14.0
  Hud.text(remember("menu_button_text_" .. button.id), button.label, text_x + 2.0, text_y + 2.0, scale, 0.17, 0.08, 0.34, 0.72)
  Hud.text(remember("menu_button_label_" .. button.id), button.label, text_x, text_y, scale, 0.94, 0.98, 1.0, 1.0)
end

function Menu.start()
  state.menu_open = true
  Runtime.set_physics_enabled(false)
end

function Menu.update(dt)
  local width, height = Input.viewport_size()
  local mouse_x, mouse_y = Input.mouse_position()
  local mouse_down = Input.mouse_down("left")
  local clicked = mouse_down and not Menu.mouse_was_down
  local enter_down = Input.is_down("return") or Input.is_down("space")
  local enter_pressed = enter_down and not Menu.enter_was_down

  draw_background(width, height)
  draw_title(width)

  local button_w = math.min(420.0, width * 0.58)
  local button_h = 52.0
  local start_y = height * 0.45
  local hovered_id = nil

  for index, button in ipairs(buttons) do
    local rect = {
      x = width * 0.5 - button_w * 0.5,
      y = start_y + (index - 1) * 70.0,
      w = button_w,
      h = button_h,
    }
    local hovered = contains(rect, mouse_x, mouse_y)
    if hovered then
      hovered_id = button.id
    end
    draw_button(button, rect, hovered)
  end

  Hud.text(remember("menu_tip"), "CLICK START OR PRESS SPACE", width * 0.5 - text_width("CLICK START OR PRESS SPACE", 2.5) * 0.5, height - 58.0, 2.5, 0.86, 0.86, 0.86, 1.0)

  if clicked and hovered_id == "start" or enter_pressed then
    start_game()
  elseif clicked and hovered_id == "quit" then
    Runtime.quit()
  end

  Menu.mouse_was_down = mouse_down
  Menu.enter_was_down = enter_down
end

return Menu
