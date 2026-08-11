local Projectile = {}

function Projectile:on_create()
  self.expires_at = Time.time + (self.lifetime or 3.0)
end

function Projectile:on_update()
  if Time.time >= self.expires_at then Entity.destroy(self.entity_id) end
end

return Projectile
