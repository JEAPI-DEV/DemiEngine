local Config = require("shooter.config")

local Combat = {}
Combat.__index = Combat

local function copy_player(player)
  return {
    id = player.id,
    health = player.health,
    score = player.score,
    deaths = player.deaths,
  }
end

local function sorted_players(players)
  local result = {}
  for _, player in pairs(players) do
    result[#result + 1] = copy_player(player)
  end
  table.sort(result, function(left, right)
    if left.score == right.score then
      return left.id < right.id
    end
    return left.score > right.score
  end)
  return result
end

local function normalized(x, y)
  local length = math.sqrt(x * x + y * y)
  if length < 0.0001 then
    return 1.0, 0.0
  end
  return x / length, y / length
end

function Combat.new(game)
  return setmetatable({
    game = game,
    players = {},
    last_shot = {},
    tracer = nil,
  }, Combat)
end

function Combat:ensure_player(id)
  if id == nil or id == "" then
    return nil
  end
  if self.players[id] == nil then
    self.players[id] = {id = id, health = 100, score = 0, deaths = 0, invulnerable_until = 0}
  end
  return self.players[id]
end

function Combat:set_local_player(id)
  self.local_id = id
  self:ensure_player(id)
end

function Combat:position(id)
  if id == self.local_id then
    return Transform.get_position(Config.player_entity)
  end
  return NetworkSession.remote_position(id)
end

function Combat:aim_direction(origin_x, origin_y)
  if not Input.ui_pointer_captured() then
    local mouse_x, mouse_y = Input.mouse_world_position()
    if mouse_x ~= nil then
      return normalized(mouse_x - origin_x, mouse_y - origin_y)
    end
  end

  local best_x, best_y, best_distance = nil, nil, math.huge
  for id, _ in pairs(self.players) do
    if id ~= self.local_id then
      local x, y = self:position(id)
      if x ~= nil then
        local dx, dy = x - origin_x, y - origin_y
        local distance = dx * dx + dy * dy
        if distance < best_distance then
          best_x, best_y, best_distance = dx, dy, distance
        end
      end
    end
  end
  if best_x ~= nil then
    return normalized(best_x, best_y)
  end
  return self.game.facing_x, self.game.facing_y
end

function Combat:show_tracer(data)
  local dx, dy = normalized(data.dx or 0, data.dy or 0)
  local distance = self:wall_distance(data.x, data.y, dx, dy)
  self.tracer = {
    x1 = data.x,
    y1 = data.y,
    x2 = data.x + dx * distance,
    y2 = data.y + dy * distance,
    expires = self.game.elapsed + 0.08,
  }
end

function Combat:wall_distance(x, y, dx, dy)
  local hit = Physics2D.raycast(
    x,
    y,
    dx,
    dy,
    Config.shot_range,
    Config.wall_layer,
    Config.player_entity
  )
  return hit ~= nil and hit.distance or Config.shot_range
end

function Combat:update_tracer()
  Debug.clear_lines()
  if self.tracer == nil or self.game.elapsed >= self.tracer.expires then
    self.tracer = nil
    return
  end
  Debug.line(
    self.tracer.x1,
    self.tracer.y1,
    self.tracer.x2,
    self.tracer.y2,
    1.0,
    0.84,
    0.32,
    1.0,
    3.0
  )
end

function Combat:request_shot()
  if self.local_id == nil then
    return
  end
  local last = self.last_shot[self.local_id] or -100
  if self.game.elapsed - last < Config.shot_cooldown then
    return
  end
  self.last_shot[self.local_id] = self.game.elapsed

  local x, y = Transform.get_position(Config.player_entity)
  if x == nil then
    return
  end
  local dx, dy = self:aim_direction(x, y)
  local data = {x = x, y = y, dx = dx, dy = dy}
  self:show_tracer(data)

  if self.game.mode == "host" then
    self:resolve_shot(self.local_id, data)
    NetworkSession.emit("shot", data, false)
  elseif self.game.mode == "client" then
    NetworkSession.emit("shot", data, false)
  end
end

function Combat:closest_hit(shooter_id, data)
  local shot_distance = data.distance or Config.shot_range
  local best_id, best_along = nil, shot_distance + 1
  for id, player in pairs(self.players) do
    if id ~= shooter_id and self.game.elapsed >= (player.invulnerable_until or 0) then
      local x, y = self:position(id)
      if x ~= nil then
        local offset_x, offset_y = x - data.x, y - data.y
        local along = offset_x * data.dx + offset_y * data.dy
        local perpendicular_x = offset_x - data.dx * along
        local perpendicular_y = offset_y - data.dy * along
        local distance_squared = perpendicular_x * perpendicular_x + perpendicular_y * perpendicular_y
        if along >= 0 and along <= shot_distance
          and distance_squared <= Config.shot_radius * Config.shot_radius
          and along < best_along then
          best_id, best_along = id, along
        end
      end
    end
  end
  return best_id
end

function Combat:state_payload(respawn)
  return {
    players = sorted_players(self.players),
    respawn = respawn,
  }
end

function Combat:broadcast_state(respawn)
  NetworkSession.emit("combat_state", self:state_payload(respawn), true)
end

function Combat:resolve_shot(shooter_id, data)
  local shooter = self:ensure_player(shooter_id)
  if shooter == nil or data == nil then
    return
  end
  data.dx, data.dy = normalized(data.dx or 0, data.dy or 0)
  local authoritative_x, authoritative_y = self:position(shooter_id)
  if authoritative_x == nil then
    return
  end
  local claimed_x = data.x or authoritative_x
  local claimed_y = data.y or authoritative_y
  local error_x = claimed_x - authoritative_x
  local error_y = claimed_y - authoritative_y
  if error_x * error_x + error_y * error_y > 2.25 then
    data.x, data.y = authoritative_x, authoritative_y
  else
    data.x, data.y = claimed_x, claimed_y
  end
  data.distance = self:wall_distance(data.x, data.y, data.dx, data.dy)

  local target_id = self:closest_hit(shooter_id, data)
  if target_id == nil then
    return
  end

  local target = self:ensure_player(target_id)
  target.health = math.max(0, target.health - Config.shot_damage)
  local respawn = nil
  if target.health == 0 then
    shooter.score = shooter.score + 1
    target.deaths = target.deaths + 1
    target.health = 100
    target.invulnerable_until = self.game.elapsed + Config.respawn_invulnerability
    local x, y = Config.spawn_for(target_id, target.deaths)
    respawn = {id = target_id, x = x, y = y}
    if target_id == self.local_id then
      Transform.set_position(Config.player_entity, x, y)
    end
  end
  self:broadcast_state(respawn)
end

function Combat:apply_state(data)
  if data == nil or data.players == nil then
    return
  end
  for _, incoming in ipairs(data.players) do
    local player = self:ensure_player(incoming.id)
    player.health = incoming.health or player.health
    player.score = incoming.score or player.score
    player.deaths = incoming.deaths or player.deaths
  end
  if data.respawn ~= nil and data.respawn.id == self.local_id then
    Transform.set_position(Config.player_entity, data.respawn.x, data.respawn.y)
  end
end

function Combat:on_join(sender_id)
  self:ensure_player(sender_id)
  if self.game.mode == "host" then
    self:broadcast_state(nil)
  end
end

function Combat:on_event(event)
  if event.name == "player_join" then
    self:on_join(event.sender_id)
  elseif event.name == "shot" and event.data ~= nil then
    self:show_tracer(event.data)
    if self.game.mode == "host" then
      local previous = self.last_shot[event.sender_id] or -100
      if self.game.elapsed - previous >= Config.shot_cooldown * 0.8 then
        self.last_shot[event.sender_id] = self.game.elapsed
        self:resolve_shot(event.sender_id, event.data)
      end
    end
  elseif event.name == "combat_state" and self.game.mode == "client" then
    self:apply_state(event.data)
  end
end

function Combat:scoreboard()
  return sorted_players(self.players)
end

return Combat
