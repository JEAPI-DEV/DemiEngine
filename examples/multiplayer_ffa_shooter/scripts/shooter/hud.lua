local HudView = {}

function HudView.show_menu(status)
  Hud.set_group_visible("network_menu", true)
  Hud.set_group_visible("match_ui", false)
  Hud.set_text("menu_status", status or "PORT 39420")
end

function HudView.show_match()
  Hud.set_group_visible("network_menu", false)
  Hud.set_group_visible("match_ui", true)
end

function HudView.status(message)
  Hud.set_text("menu_status", message)
end

function HudView.update(game)
  local local_player = game.combat.players[game.local_id]
  if local_player == nil then
    return
  end

  Hud.set_text("health_value", tostring(local_player.health))
  Hud.set_text("score_value", "SCORE " .. tostring(local_player.score) .. " / " .. tostring(local_player.deaths))

  if game.mode == "practice" then
    Hud.set_text("network_value", "PRACTICE")
  else
    local diagnostics = NetworkSession.diagnostics()
    local role = game.mode == "host" and "HOST" or "CLIENT"
    Hud.set_text("network_value", role .. "  " .. tostring(diagnostics.latency_ms) .. " MS")
  end

  local rows = {}
  for index, player in ipairs(game.combat:scoreboard()) do
    if index > 4 then
      break
    end
    local marker = player.id == game.local_id and ">" or ""
    rows[#rows + 1] = marker .. player.id .. " " .. tostring(player.score)
  end
  Hud.set_text("scoreboard", table.concat(rows, "   "))
end

return HudView
