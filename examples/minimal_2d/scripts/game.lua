local Game = {
  points = 0,
}

function Game:on_create()
end

function Game:on_start()
  Hud.text("points", "POINTS: " .. tostring(self.points), 24.0, 24.0, 3.0)
end

function Game:add_points(amount)
  self.points = self.points + amount
  Hud.set_text("points", "POINTS: " .. tostring(self.points))
end

function Game:on_update(dt)
  if Input.is_down("p") and not self.p_was_down then
    self:add_points(1)
  end

  self.p_was_down = Input.is_down("p")
end

function Game:on_fixed_update(dt)
end

function Game:on_destroy()
end

return Game
