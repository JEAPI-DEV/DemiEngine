local Pickup = {}

function Pickup:on_create()
  self.subscription = Events.subscribe("physics3d_trigger_enter", function(hit)
    if hit.entity_id == self.entity_id or hit.other_entity_id == self.entity_id then
      Events.emit("pickup_collected", { entity_id = self.entity_id })
      Entity.destroy(self.entity_id)
    end
  end)
end

function Pickup:on_destroy()
  if self.subscription then Events.unsubscribe(self.subscription) end
end

return Pickup
