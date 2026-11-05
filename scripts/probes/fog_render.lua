local Fog = require("demi.visibility.fog2d")
local Entity = require("demi.entity")
local Application = require("demi.application")
local Transform = require("demi.transform2d")
local Probe = {}
function Probe:on_start()
  self.time = 0
  Application.set_max_fps(60)
  assert(Fog.configure(24, 24, 1, -12, -12, 0, 0, 0, 0.45))
  Fog.reveal(13, 18)
  assert(Fog.update(0, 0, 11, 1.4, "wall"))
end
function Probe:on_update(dt)
  self.time = self.time + dt
  if self.time > 0.5 and not self.first then
    self.first = true
    print("FOG_CAPTURE_CLOSED")
  end
  if self.time > 0.9 and not self.remembered then
    self.remembered = true
    Fog.reveal(14, 18)
  end
  if self.time > 1.2 and not self.memoryCapture then
    self.memoryCapture = true
    print("FOG_CAPTURE_MEMORY")
  end
  if self.time > 1.5 and not self.open then
    self.open = true
    assert(Entity.set_enabled("wall", false))
  end
  if self.time > 1.7 and not self.refreshed then
    self.refreshed = true
    assert(not Entity.is_enabled("wall"))
    assert(Fog.update(0, 0, 11, 1.4, "wall"))
  end
  if self.time > 2 and not self.second then
    self.second = true
    print("FOG_CAPTURE_OPEN")
  end
  if self.time > 2.3 and not self.corridor then
    self.corridor = true
    for index = 0, 5 do
      assert(Entity.set_enabled("corridor_" .. index, true))
    end
  end
  if self.time > 2.6 and not self.corridorRefreshed then
    self.corridorRefreshed = true
    assert(Fog.update(0, 0, 11, 1.4, "wall"))
  end
  if self.time > 2.9 and not self.corridorCapture then
    self.corridorCapture = true
    print("FOG_CAPTURE_CORRIDOR")
  end
  if self.time > 3.2 and not self.shifted then
    self.shifted = true
    for index = 0, 5 do
      assert(Entity.set_enabled("corridor_" .. index, false))
    end
    assert(Transform.set_position("player", 0.37, -0.19))
    assert(Transform.set_position("enemy", 3, -0.19))
    assert(Transform.set_rotation("enemy", 0.7))
  end
  if self.time > 3.5 and not self.shiftedRefreshed then
    self.shiftedRefreshed = true
    assert(Fog.update(0.37, -0.19, 9.7, 1.4, "wall"))
  end
  if self.time > 3.8 and not self.shiftedCapture then
    self.shiftedCapture = true
    print("FOG_CAPTURE_SHIFTED")
  end
  if self.time > 4.5 then
    Application.quit()
  end
end
return Probe
