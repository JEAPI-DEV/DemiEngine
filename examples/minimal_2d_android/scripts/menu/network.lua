local lobby = require("network_lobby")
local replication = require("network_replication")

local NetworkMenu = {
  lobbies = {}, page = 1, page_size = 4,
  create_name = "", create_private = false, create_password = "",
  join_password = "", selected_lobby = nil, focus = nil,
  admission_token = nil, auth_sent = false,
  create_pending = false,
  create_connect_pending = false,
}

local menu
local callbacks

local function status(text) Hud.set_text("menu_network_status", text or "") end
local function group(name, visible) Hud.set_group_visible(name, visible) end
local function masked(value) return string.rep("*", #value) end

local function show_browser()
  menu.screen = "network"
  group("menu_network", true); group("network_browser", true)
  group("network_create", false); group("network_password", false)
  group("network_empty", #NetworkMenu.lobbies == 0)
  NetworkMenu.focus = nil
end

local function render_lobbies()
  local first = (NetworkMenu.page - 1) * NetworkMenu.page_size + 1
  for row = 1, NetworkMenu.page_size do
    local item = NetworkMenu.lobbies[first + row - 1]
    local id = "network_lobby_" .. tostring(row)
    Hud.set_visible(id, item ~= nil)
    if item ~= nil then
      local privacy = item.private and "[LOCKED]" or "[PUBLIC]"
      Hud.set_button_label(id, privacy .. "  " .. item.name .. "  " .. tostring(item.players) .. "/" .. tostring(item.max_players))
    end
  end
  local pages = math.max(1, math.ceil(#NetworkMenu.lobbies / NetworkMenu.page_size))
  Hud.set_text("network_page", tostring(NetworkMenu.page) .. "/" .. tostring(pages))
  local empty = #NetworkMenu.lobbies == 0
  group("network_empty", empty)
  status(empty and "NO LOBBIES FOUND" or "SELECT A LOBBY")
end

local function begin_game(token)
  NetworkSession.configure({
    trusted_certificate = lobby.trusted_certificate,
    server_name = lobby.server_name,
  })
  NetworkMenu.admission_token = token
  NetworkMenu.auth_sent = false
  menu.network_join_pending = replication.connect(lobby.server_host, lobby.gameplay_port)
  menu.network_join_elapsed = 0.0
  status(menu.network_join_pending and "CONNECTING TO GAME" or "GAME SERVER UNAVAILABLE")
end

function NetworkMenu.bind(bound_menu, bound_callbacks)
  menu = bound_menu; callbacks = bound_callbacks
  return NetworkMenu
end

function NetworkMenu.open()
  show_browser()
  status("CONNECTING TO MATCHMAKING")
  if not lobby.connect() then status("TLS CONNECTION FAILED") end
end

function NetworkMenu.refresh()
  if lobby.connected then status("REFRESHING"); lobby.list()
  elseif not lobby.connect() then status("TLS CONNECTION FAILED") end
end

function NetworkMenu.show_create()
  menu.screen = "network_create"
  group("network_browser", false); group("network_empty", false); group("network_password", false); group("network_create", true)
  Hud.set_button_label("network_create_name", "NAME: " .. (NetworkMenu.create_name ~= "" and NetworkMenu.create_name or "_"))
  Hud.set_button_label("network_create_password", "PASSWORD: " .. masked(NetworkMenu.create_password))
  Hud.set_button_label("network_create_visibility", NetworkMenu.create_private and "PRIVATE" or "PUBLIC")
  Hud.set_visible("network_create_password", NetworkMenu.create_private)
  status("CREATE A LOBBY")
end

function NetworkMenu.focus_name() NetworkMenu.focus = "name"; Input.set_text_input_active(true); status("TYPE LOBBY NAME") end
function NetworkMenu.focus_create_password() NetworkMenu.focus = "create_password"; Input.set_text_input_active(true); status("TYPE PRIVATE PASSWORD") end
function NetworkMenu.focus_join_password() NetworkMenu.focus = "join_password"; Input.set_text_input_active(true); status("TYPE PRIVATE PASSWORD") end
function NetworkMenu.toggle_private()
  NetworkMenu.create_private = not NetworkMenu.create_private
  NetworkMenu.show_create()
end
function NetworkMenu.cancel_form() Input.set_text_input_active(false); show_browser(); render_lobbies() end

function NetworkMenu.submit_create()
  if NetworkMenu.create_pending then return end
  if NetworkMenu.create_name == "" or #NetworkMenu.create_name > 20 or NetworkMenu.create_name:match("^[a-z0-9_]+$") == nil then
    status("NAME: A-Z 0-9 _ / MAX 20")
    return
  end
  if NetworkMenu.create_private and (#NetworkMenu.create_password < 1 or #NetworkMenu.create_password > 64) then
    status("PASSWORD REQUIRED / MAX 64")
    return
  end
  if not lobby.connected then
    NetworkMenu.create_connect_pending = lobby.connect()
    status(NetworkMenu.create_connect_pending and "CONNECTING TO MATCHMAKING" or "TLS CONNECTION FAILED")
    return
  end
  if not lobby.create(NetworkMenu.create_name, NetworkMenu.create_private, NetworkMenu.create_password) then
    status("MATCHMAKING NOT CONNECTED")
    return
  end
  NetworkMenu.create_pending = true
  status("CREATING LOBBY")
  Input.set_text_input_active(false)
end

function NetworkMenu.join_row(row)
  local item = NetworkMenu.lobbies[(NetworkMenu.page - 1) * NetworkMenu.page_size + row]
  if item == nil then return end
  NetworkMenu.selected_lobby = item
  if item.private then
    menu.screen = "network_password"
    group("network_browser", false); group("network_empty", false); group("network_create", false); group("network_password", true)
    NetworkMenu.join_password = ""; NetworkMenu.focus = "join_password"
    Hud.set_text("network_password_title", "JOIN " .. item.name)
    Hud.set_button_label("network_join_password", "PASSWORD: ")
    status("PASSWORD REQUIRED")
  else
    status("JOINING " .. item.name)
    lobby.join(item.id, "")
  end
end

function NetworkMenu.submit_join()
  if NetworkMenu.selected_lobby ~= nil then
    status("JOINING " .. NetworkMenu.selected_lobby.name)
    Input.set_text_input_active(false)
    lobby.join(NetworkMenu.selected_lobby.id, NetworkMenu.join_password)
  end
end

function NetworkMenu.previous_page()
  if NetworkMenu.page > 1 then NetworkMenu.page = NetworkMenu.page - 1; render_lobbies() end
end
function NetworkMenu.next_page()
  if NetworkMenu.page * NetworkMenu.page_size < #NetworkMenu.lobbies then NetworkMenu.page = NetworkMenu.page + 1; render_lobbies() end
end

local function edit_text()
  if NetworkMenu.focus == nil then return end
  local key = NetworkMenu.focus
  local value = key == "name" and NetworkMenu.create_name
    or key == "create_password" and NetworkMenu.create_password or NetworkMenu.join_password
  if Input.is_pressed("backspace") then value = value:sub(1, math.max(0, #value - 1)) end
  local entered = Input.text_entered()
  if key == "name" then entered = entered:lower():gsub("[^a-z0-9_]", "") end
  local limit = key == "name" and 20 or 64
  value = (value .. entered):sub(1, limit)
  if key == "name" then
    NetworkMenu.create_name = value; Hud.set_button_label("network_create_name", "NAME: " .. (value ~= "" and value or "_"))
  elseif key == "create_password" then
    NetworkMenu.create_password = value; Hud.set_button_label("network_create_password", "PASSWORD: " .. masked(value))
  else
    NetworkMenu.join_password = value; Hud.set_button_label("network_join_password", "PASSWORD: " .. masked(value))
  end
end

function NetworkMenu.update_lobby(dt)
  lobby.update(dt)
  edit_text()
  if NetworkMenu.create_connect_pending and lobby.connected then
    NetworkMenu.create_connect_pending = false
    NetworkMenu.submit_create()
  end
  for _, message in ipairs(lobby.events()) do
    local payload = message.payload or {}
    if message.type == "lobby_list" then
      NetworkMenu.lobbies = payload.lobbies or {}; NetworkMenu.page = 1
      show_browser(); render_lobbies()
    elseif message.type == "lobby_created" or message.type == "lobby_joined" then
      NetworkMenu.create_pending = false
      begin_game(payload.admission_token)
    elseif message.type == "error" then
      NetworkMenu.create_pending = false
      NetworkMenu.create_connect_pending = false
      status((payload.message or payload.code or "MATCHMAKING ERROR"):upper())
      if payload.code == "wrong_password" then NetworkMenu.focus = "join_password" end
    end
  end
end

function NetworkMenu.update_connection(dt)
  if not menu.network_join_pending then return end
  menu.network_join_elapsed = menu.network_join_elapsed + dt
  local events = replication.process_events()
  if events.connected and not NetworkMenu.auth_sent then
    NetworkMenu.auth_sent = Network.send(Network.encode("lobby_auth", { token = NetworkMenu.admission_token }), true)
  end
  local session = events.session or replication.current_session()
  if session ~= nil and session.scene_id ~= nil then
    menu.network_join_pending = false
    callbacks.select_level(session.scene_id, session.level or 1)
  elseif events.disconnected or menu.network_join_elapsed >= menu.network_join_timeout then
    menu.network_join_pending = false; status("GAME SERVER DISCONNECTED")
  end
end

function NetworkMenu.disconnect()
  lobby.leave(); lobby.disconnect(); replication.disconnect()
  menu.network_join_pending = false
end
function NetworkMenu.back_to_main() NetworkMenu.disconnect(); callbacks.show_main() end

return NetworkMenu
