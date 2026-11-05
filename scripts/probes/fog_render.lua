local Fog = require("demi.visibility.fog2d")
local Entity = require("demi.entity")
local Application = require("demi.application")
local Transform = require("demi.transform2d")
local Probe = { phase = "closed" }

function Probe:on_start()
  self.time = 0
  Application.set_max_fps(60)
  assert(Fog.configure(24, 24, 1, -12, -12, 0, 0, 0, 0.45))
  Fog.reveal(13, 18)
  if self.phase == "open" or self.phase == "corridor" or self.phase == "shifted" then
    assert(Entity.set_enabled("wall", false))
  end
  if self.phase == "corridor" then
    for index = 0, 5 do
      assert(Entity.set_enabled("corridor_" .. index, true))
    end
  elseif self.phase == "shifted" then
    assert(Transform.set_position("player", 0.37, -0.19))
    assert(Transform.set_position("enemy", 3, -0.19))
    assert(Transform.set_rotation("enemy", 0.7))
  end
end

function Probe:on_update(dt)
  self.time = self.time + dt
  -- Allow queued entity changes to publish before querying the static fixture.
  if self.time > 0.2 and not self.refreshed then
    self.refreshed = true
    if self.phase == "shifted" then
      assert(Fog.update(0.37, -0.19, 9.7, 1.4, "wall"))
    else
      assert(Fog.update(0, 0, 11, 1.4, "wall"))
    end
  end
  if self.phase == "memory" and self.time > 0.35 and not self.remembered then
    self.remembered = true
    -- Discovery publication must work without another sight-geometry update.
    Fog.reveal(14, 18)
  end
  if self.time > 0.6 and not self.captured then
    self.captured = true
    print("FOG_CAPTURE_" .. string.upper(self.phase))
  end
  -- The host captures then terminates this owned process. Keep the fixture
  -- unchanged regardless of screenshot latency or drawable size.
end

return Probe
