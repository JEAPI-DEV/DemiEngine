-- Run an update callback from 0.0 to 1.0. Call update(dt) from on_update.
local GuiAnimation = {}
GuiAnimation.__index = GuiAnimation

function GuiAnimation.new()
  return setmetatable({ active = {} }, GuiAnimation)
end

function GuiAnimation:play(id, duration, update, loop)
  local animation = {
    duration = math.max(duration, 0.0001),
    elapsed = 0.0,
    update = update,
    loop = loop == true,
  }
  self.active[id] = animation
  update(0.0, 0.0)
end

function GuiAnimation:stop(id)
  self.active[id] = nil
end

function GuiAnimation:update(dt)
  for id, animation in pairs(self.active) do
    animation.elapsed = animation.elapsed + math.max(dt, 0.0)
    local progress = animation.elapsed / animation.duration
    if animation.loop then
      progress = progress % 1.0
    else
      progress = math.min(progress, 1.0)
    end

    animation.update(progress, animation.elapsed)
    if not animation.loop and progress == 1.0 then
      self.active[id] = nil
    end
  end
end

return GuiAnimation
