local Encounters = {}
Encounters.__index = Encounters

function Encounters.new(events, spawn)
  return setmetatable({ events = assert(events), spawn = assert(spawn), waves = {}, wave = 0,
    pending = {}, alive = {}, completed = false, failed = false }, Encounters)
end

function Encounters:configure(waves) self.waves = waves or {} end

function Encounters:start_next()
  if self.failed or self.completed then return false end
  self.wave = self.wave + 1
  local definition = self.waves[self.wave]
  if not definition then self.completed = true; self.events:emit("encounter_completed", {}); return false end
  self.pending = {}
  for index, spawn in ipairs(definition.spawns or {}) do
    self.pending[index] = { at = spawn.at or 0, prefab = spawn.prefab, entrance = spawn.entrance, order = index }
  end
  self.events:emit("wave_started", { wave = self.wave }); return true
end

function Encounters:update(elapsed)
  for index = #self.pending, 1, -1 do
    local request = self.pending[index]
    if request.at <= elapsed then
      local id, reason = self.spawn(request)
      if id then self.alive[id] = true; self.events:emit("spawn_requested", { entity = id, wave = self.wave })
      else self.events:emit("spawn_failed", { wave = self.wave, reason = reason, order = request.order }) end
      table.remove(self.pending, index)
    end
  end
end

function Encounters:defeated(id) self.alive[id] = nil end
function Encounters:ready_for_next() return #self.pending == 0 and next(self.alive) == nil end
function Encounters:fail(reason)
  if self.failed then return false end
  self.failed = true; self.events:emit("objective_failed", { reason = reason }); return true
end

return Encounters
