local Save = require("demi.save")
local Checkpoint = require("dungeon.checkpoint")
local Persistence = { slot = "hollow_crown_checkpoint" }

function Persistence.exists()
  return Save.exists(Persistence.slot)
end

function Persistence.write(run)
  if run.phase ~= "playing" and run.phase ~= "upgrade" then
    return false, "Only an active expedition can be saved."
  end
  if Save.exists(Persistence.slot) then
    local previous = Save.read(Persistence.slot)
    if
      not previous
      or (
        previous.format_version ~= 1
        and previous.format_version ~= 2
        and previous.format_version ~= 3
      )
    then
      return false, "Existing checkpoint is incompatible; it has been preserved."
    end
    local valid = Checkpoint.decode(previous)
    if not valid then
      return false, "Existing checkpoint is invalid; it has been preserved."
    end
    if previous.format_version < 3 then
      local backup = Persistence.slot .. "_v" .. previous.format_version .. "_backup"
      if not Save.exists(backup) and not Save.write(backup, previous, previous.format_version) then
        return false, "Could not back up the old checkpoint; it has been preserved."
      end
    end
  end
  if not Save.write(Persistence.slot, Checkpoint.encode(run), 3) then
    return false, "Save failed: " .. Save.last_error()
  end
  return true, "Expedition saved. Continue from the title screen."
end

function Persistence.read()
  local saved = Save.read(Persistence.slot)
  if not saved then
    return nil, "No readable checkpoint: " .. Save.last_error()
  end
  return Checkpoint.decode(saved)
end

return Persistence
