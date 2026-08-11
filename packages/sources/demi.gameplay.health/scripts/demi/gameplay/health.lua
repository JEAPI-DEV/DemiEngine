local Health = {}
Health.__index = Health

function Health.new(events, options)
  return setmetatable({ events = assert(events), entries = {}, policy = options and options.policy }, Health)
end

function Health.team_policy(options)
  options = options or {}
  return function(request, target, amount, service)
    local source = request.source and service:get(request.source) or nil
    if request.source == request.target and options.self_damage == false then return 0 end
    if source and source.team and target.team and source.team == target.team and
      options.friendly_fire ~= true then return 0 end
    return amount
  end
end

function Health:add(id, maximum, options)
  assert(type(id) == "string" and maximum > 0)
  options = options or {}
  self.entries[id] = { current = math.min(options.current or maximum, maximum), maximum = maximum,
    invulnerable = options.invulnerable == true, team = options.team }
  return self.entries[id]
end

function Health:get(id) return self.entries[id] end
function Health:remove(id) self.entries[id] = nil end
function Health:set_invulnerable(id, value)
  local entry = self.entries[id]
  if entry then entry.invulnerable = value == true end
end

function Health:damage(request)
  local target = self.entries[request.target]
  if not target then return false, "missing_target" end
  local amount = math.max(0, tonumber(request.amount) or 0)
  if amount == 0 then return false, "zero_damage" end
  if target.invulnerable then return false, "invulnerable" end
  if self.policy then amount = self.policy(request, target, amount, self) end
  amount = math.max(0, tonumber(amount) or 0)
  if amount == 0 then return false, "blocked" end
  local previous = target.current
  target.current = math.max(0, previous - amount)
  self.events:emit("damage_applied", { source = request.source, target = request.target,
    amount = previous - target.current, type = request.type, point = request.point, tags = request.tags })
  self.events:emit("health_changed", { entity = request.target, previous = previous,
    current = target.current, maximum = target.maximum })
  if previous > 0 and target.current == 0 then
    self.events:emit("entity_defeated", { entity = request.target, source = request.source })
  end
  return true, previous - target.current
end

function Health:heal(id, amount)
  local target = self.entries[id]
  if not target then return false end
  local previous = target.current
  target.current = math.min(target.maximum, previous + math.max(0, tonumber(amount) or 0))
  if target.current ~= previous then
    self.events:emit("health_changed", { entity = id, previous = previous,
      current = target.current, maximum = target.maximum })
  end
  return true, target.current - previous
end

return Health
