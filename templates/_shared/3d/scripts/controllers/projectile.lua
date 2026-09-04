local Script = require("demi.script")
local Projectile = {}

function Projectile:on_create()
  Script.bind(self)
  -- Lifetime owned by the spawner ttl; this is a fallback for direct spawns.
  self:after(self.lifetime or 3.0, function()
    Entity.destroy(self.entity_id)
  end)
end

function Projectile:on_destroy()
  Script.release(self)
end

-- Re-export Script helper methods so self:after works.
for key, value in pairs(Script) do
  if Projectile[key] == nil then
    Projectile[key] = value
  end
end

return Projectile
