local replication = require("network_replication")

local NetworkMenu = {}

function NetworkMenu.bind(menu, callbacks)
  NetworkMenu.menu = menu
  NetworkMenu.callbacks = callbacks
  return NetworkMenu
end

function NetworkMenu.update_hud(status)
  local menu = NetworkMenu.menu
  Hud.set_text("menu_network_ip_value", menu.network_ip == "" and "_" or menu.network_ip)
  Hud.set_text("menu_network_level", "HOST LEVEL " .. tostring(menu.network_level) .. "  -  F1/F2")
  if status ~= nil then
    Hud.set_text("menu_network_status", status)
  end
end

function NetworkMenu.host_game()
  local menu = NetworkMenu.menu
  local callbacks = NetworkMenu.callbacks
  menu.network_join_pending = false
  if replication.host(menu.network_port) then
    local scene_id = callbacks.scene_for_level(menu.network_level)
    replication.start_session({
      scene_id = scene_id,
      level = menu.network_level,
      seed = 1000 + menu.network_level,
    })
    NetworkMenu.update_hud("HOSTING LEVEL " .. tostring(menu.network_level) .. " ON PORT " .. tostring(menu.network_port))
    callbacks.select_level(scene_id, menu.network_level)
  else
    NetworkMenu.update_hud("HOST FAILED - ENABLE NETWORK BUILD")
  end
end

function NetworkMenu.join_game()
  local menu = NetworkMenu.menu
  if menu.network_ip == "" then
    NetworkMenu.update_hud("ENTER SERVER IP")
    return
  end
  if replication.connect(menu.network_ip, menu.network_port) then
    menu.network_join_pending = true
    menu.network_join_elapsed = 0.0
    NetworkMenu.update_hud("CONNECTING TO " .. menu.network_ip)
  else
    menu.network_join_pending = false
    NetworkMenu.update_hud("JOIN FAILED - CHECK IP OR BUILD")
  end
end

function NetworkMenu.back_to_main()
  local menu = NetworkMenu.menu
  if menu.network_join_pending then
    replication.disconnect()
    menu.network_join_pending = false
  end
  NetworkMenu.callbacks.show_main()
end

function NetworkMenu.update_connection(dt)
  local menu = NetworkMenu.menu
  local callbacks = NetworkMenu.callbacks
  if not menu.network_join_pending then
    return
  end

  menu.network_join_elapsed = menu.network_join_elapsed + dt
  local events = replication.process_events()
  local session = events.session or replication.current_session()
  if session ~= nil and session.scene_id ~= nil then
    menu.network_join_pending = false
    NetworkMenu.update_hud("CONNECTED LEVEL " .. tostring(session.level or 1))
    callbacks.select_level(session.scene_id, session.level or 1)
    return
  end

  if events.connected or replication.is_connected() then
    NetworkMenu.update_hud("CONNECTED - WAITING FOR SESSION")
    return
  end

  if events.disconnected then
    menu.network_join_pending = false
    replication.disconnect()
    NetworkMenu.update_hud("JOIN FAILED - NO HOST")
    return
  end

  if menu.network_join_elapsed >= menu.network_join_timeout then
    menu.network_join_pending = false
    replication.disconnect()
    NetworkMenu.update_hud("JOIN TIMED OUT")
  end
end

function NetworkMenu.update_input(pressed_once)
  local menu = NetworkMenu.menu

  local function append_ip_char(value)
    if string.len(menu.network_ip) >= 15 then
      return
    end
    menu.network_ip = menu.network_ip .. value
    NetworkMenu.update_hud(nil)
  end

  for digit = 0, 9 do
    local key = tostring(digit)
    if pressed_once(key) then
      append_ip_char(key)
    end
  end

  if pressed_once(".") or pressed_once("period") then
    append_ip_char(".")
  end

  if pressed_once("backspace") and string.len(menu.network_ip) > 0 then
    menu.network_ip = string.sub(menu.network_ip, 1, string.len(menu.network_ip) - 1)
    NetworkMenu.update_hud(nil)
  end

  if pressed_once("delete") then
    menu.network_ip = ""
    NetworkMenu.update_hud(nil)
  end

  if pressed_once("f1") then
    menu.network_level = 1
    NetworkMenu.update_hud(nil)
  elseif pressed_once("f2") then
    menu.network_level = 2
    NetworkMenu.update_hud(nil)
  end
end

return NetworkMenu
