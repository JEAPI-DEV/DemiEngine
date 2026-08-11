local Door = {}

function Door:on_create()
  self.open = false
  self.subscription = Events.subscribe("interact", function(event)
    if event.target_id == self.entity_id then
      self.open = not self.open
      local instance = self.entity_id:gsub("/root$", "")
      Entity.set_enabled(instance .. "/solid", not self.open)
    end
  end)
end

function Door:on_destroy()
  if self.subscription then Events.unsubscribe(self.subscription) end
end

return Door
