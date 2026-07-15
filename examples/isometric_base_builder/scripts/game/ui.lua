local TowerStats = require("game.tower_stats")
local Ui = {}

local tower_menu_offsets = {
  tower_menu_bg = { 0, 0 },
  tower_stats = { 12, 10 },
  tower_upgrade = { 8, 38 },
  upgrade_icon = { 10, 43 },
  tower_destroy = { 8, 82 },
  destroy_icon = { 10, 87 },
}

local upgrade_menu_offsets = {
  upgrade_choice_bg = { 0, 0 },
  upgrade_choice_title = { 12, 10 },
  upgrade_range = { 8, 42 },
  upgrade_power = { 8, 86 },
}

local function set_group_visible(group, value)
  Hud.set_group_visible(group, value)
end

local function position_group(offsets, x, y)
  for id, offset in pairs(offsets) do
    Hud.set_position(id, x + offset[1], y + offset[2])
  end
end

local function position_context_menu(tower)
  if not tower then return end

  local world_x, world_y = Grid.tile_to_world(tower.x, tower.y, 0.0)
  local viewport_width, viewport_height = Input.viewport_size()
  viewport_width = viewport_width or 960
  viewport_height = viewport_height or 540
  local canvas_width, canvas_height = Hud.canvas_size()
  canvas_width = canvas_width or 960
  canvas_height = canvas_height or 540

  local half_height = 13.0
  local half_width = half_height * viewport_width / viewport_height
  local screen_x = (world_x / (half_width * 2) + 0.5) * canvas_width
  local screen_y = (0.5 - world_y / (half_height * 2)) * canvas_height

  local menu_x = math.max(12, math.min(canvas_width - 196, screen_x - 88))
  local menu_y = math.max(82, math.min(canvas_height - 178, screen_y - 142))
  local choice_x = math.max(12, menu_x - 190)

  position_group(tower_menu_offsets, menu_x, menu_y)
  position_group(upgrade_menu_offsets, choice_x, menu_y)
end

function Ui.update(state, config)
  local enemy_count = 0
  for _ in pairs(state.enemies) do enemy_count = enemy_count + 1 end

  Hud.set_text("gold_value", "GOLD " .. tostring(state.gold))
  Hud.set_text("base_value", "KEEP " .. tostring(state.base_health))
  Hud.set_text("wave_value", "WAVE " .. tostring(state.wave))
  Hud.set_text("enemy_value", "ENEMIES " .. tostring(enemy_count + state.spawn_remaining))
  Hud.set_text("status_value", state.status)

  local tower = state.selected_id and state.towers[state.selected_id]
  local definition = tower and config.towers[tower.kind]
  local stats = tower and definition and TowerStats.get(tower, definition)

  if stats then
    Hud.set_text("selection_value", "SELECTED: " .. string.upper(definition.label) .. " LV " .. stats.level)
    Hud.set_text("tower_stats", "LV " .. stats.level .. "/" .. definition.upgrades.max_level .. "  POW " .. stats.damage .. "  RNG " .. string.format("%.2f", stats.range))

    local cost = stats.level < definition.upgrades.max_level and TowerStats.upgrade_cost(tower, definition) or 0
    Hud.set_text("tower_upgrade", stats.level >= definition.upgrades.max_level and "MAX LEVEL" or ("UPGRADE " .. cost))

    local refund = math.floor(TowerStats.invested_gold(tower, definition) * definition.upgrades.destroy_refund + 0.5)
    Hud.set_text("tower_destroy", "DESTROY +" .. refund)

    local upgrade_disabled = stats.level >= definition.upgrades.max_level or state.gold < cost
    Hud.set_disabled("tower_upgrade", upgrade_disabled)
    Hud.set_opacity("upgrade_icon", upgrade_disabled and 0.4 or 1.0)
    Hud.set_disabled("upgrade_range", upgrade_disabled)
    Hud.set_disabled("upgrade_power", upgrade_disabled)

    position_context_menu(tower)
  else
    Hud.set_text("selection_value", "SELECTED: NONE")
  end

  set_group_visible("tower_menu", tower ~= nil)
  set_group_visible("upgrade_choice_menu", tower ~= nil and state.upgrade_menu_open)

  Hud.set_disabled("tower_destroy", tower == nil)
  Hud.set_disabled("build_arrow", state.wave_active or state.gold < config.towers.arrow.cost)
  Hud.set_disabled("build_wizard", state.wave_active or state.gold < config.towers.wizard.cost)
  Hud.set_disabled("start_wave", state.wave_active or state.base_health <= 0)
end

return Ui
