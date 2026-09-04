local Script = require("demi.script")
local Pickup = {}

function Pickup:on_create()
  Script.bind(self)
  -- Typed trigger helper: no manual entity_id/other_entity_id matching,
  -- no manual unsubscribe table.
  self:on_trigger_helper()
end

function Pickup:on_trigger_helper()
  local sub_2d, sub_3d = Physics.on_trigger(self.entity_id, function(_hit)
    Events.emit("pickup_collected", { entity_id = self.entity_id })
    Entity.destroy(self.entity_id)
  end)
  self.trigger_subs = { sub_2d, sub_3d }
end

function Pickup:on_destroy()
  Script.release(self)
end

-- Re-export Script helper methods so self:on works.
for key, value in pairs(Script) do
  if Pickup[key] == nil then
    Pickup[key] = value
  end
end

return Pickup
