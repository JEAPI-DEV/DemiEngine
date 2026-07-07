local SaveGame = {}

local SLOT = "commander"

function SaveGame.read(state)
  local data = Save.read(SLOT)
  if data == nil then
    return
  end
  if data.credits ~= nil then
    state.commander.credits = data.credits
  end
  if data.fuel ~= nil then
    state.commander.fuel = data.fuel
  end
  if data.system_index ~= nil then
    state.system_index = data.system_index
  end
  if data.selected_system ~= nil then
    state.selected_system = data.selected_system
  end
  if data.cargo ~= nil then
    for good, amount in pairs(data.cargo) do
      state.commander.cargo[good] = amount
    end
  end
end

function SaveGame.write(state)
  Save.write(SLOT, {
    format_version = 1,
    credits = state.commander.credits,
    fuel = state.commander.fuel,
    system_index = state.system_index,
    selected_system = state.selected_system,
    cargo = state.commander.cargo,
  })
end

return SaveGame
