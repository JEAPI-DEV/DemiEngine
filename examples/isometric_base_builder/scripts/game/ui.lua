local Ui = {}

function Ui.update(state)
  local enemy_count = 0
  for _ in pairs(state.enemies) do enemy_count = enemy_count + 1 end
  Hud.set_text("gold_value", "GOLD " .. tostring(state.gold))
  Hud.set_text("base_value", "KEEP " .. tostring(state.base_health))
  Hud.set_text("wave_value", "WAVE " .. tostring(state.wave))
  Hud.set_text("enemy_value", "ENEMIES " .. tostring(enemy_count + state.spawn_remaining))
  Hud.set_text("status_value", state.status)
  Hud.set_text("selection_value", "SELECTED: " .. (state.selected_id or "NONE"))
  Hud.set_disabled("build_arrow", state.wave_active or state.gold < 50)
  Hud.set_disabled("build_cannon", state.wave_active or state.gold < 90)
  Hud.set_disabled("start_wave", state.wave_active or state.base_health <= 0)
end

return Ui
