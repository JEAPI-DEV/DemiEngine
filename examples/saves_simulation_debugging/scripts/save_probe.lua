local Probe = {
  score = 0,
  sequence = 0,
  roll = 0,
}

local SLOT = "campaign"
local PLAYER = "ent_player"

local function update_status(self, prefix)
  Hud.set_text("save_status", prefix .. " | score=" .. tostring(self.score) .. " roll=" .. tostring(self.roll))
end

local function save(self, reason)
  local x, y = Entity.get_position(PLAYER)
  self.sequence = self.sequence + 1
  local ok = Save.write_state(SLOT, {
    game = { score = self.score, random_state = Random.state() },
    selected_entities = { player = PLAYER },
    prefab_instances = { player = "prefab://debug/player" },
    lua = { save_probe = { roll = self.roll, position = { x, y } } },
  }, {
    autosave = true,
    sequence = self.sequence,
    reason = reason,
  })
  update_status(self, ok and "Saved" or Save.last_error())
end

function Probe:on_start()
  Save.register_migration(1, 2, function(state)
    state.lua.save_probe.roll = state.lua.save_probe.roll or 0
    return state
  end)
  local state = Save.read_state(SLOT)
  if state == nil then
    update_status(self, "No save yet")
    return
  end
  self.score = state.game.score or 0
  self.roll = state.lua.save_probe.roll or 0
  local position = state.lua.save_probe.position
  if position then
    Entity.set_position(PLAYER, position[1], position[2])
  end
  if state.game.random_state then
    Random.restore(state.game.random_state)
  end
  local metadata = Save.metadata(SLOT)
  self.sequence = metadata and metadata.sequence or 0
  update_status(self, "Loaded " .. (metadata and metadata.reason or "save"))
end

function Probe:on_update(dt)
  local movement = Input.action_value("move")
  if movement ~= 0 then
    Entity.add_position(PLAYER, movement * dt * 3.0, 0.0)
    self.score = self.score + 1
  end
  if Input.action_pressed("randomize") then
    self.roll = Random.integer(1, 100)
    update_status(self, "Deterministic roll")
  end
  if Input.action_pressed("save") then
    save(self, "manual_checkpoint")
  end
end

return Probe
