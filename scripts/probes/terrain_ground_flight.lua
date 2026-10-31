local Transform = require("demi.transform3d")
local Physics = require("demi.physics.query3d")
local Profile = require("demi.profile")
local Hud = require("demi.hud")
local Flight = {}

-- Installed only into the profiling tool's private project copy.
function Flight:on_start()
  Hud.set_text("flight_title", "ALPINE GROUND-FLIGHT PROFILE")
  Hud.set_text("flight_controls", "Automated route · 3 m above ground · Keep panel sizes fixed")
end

function Flight:on_update(dt)
  if not self.elapsed then
    if not Physics.raycast(0, 6000, 0, 0, -1, 0, 12000) then
      return
    end
    self.elapsed = 0
  end
  local t = self.elapsed + dt
  local phase, fraction = "opening", 0
  if t >= 10 and t < 35 then
    phase, fraction = "outbound", (t - 10) / 25
  elseif t >= 35 and t < 45 then
    phase, fraction = "settled", 1
  elseif t >= 45 and t < 70 then
    phase, fraction = "return", 1 - (t - 45) / 25
  elseif t >= 70 then
    phase, fraction = "returned", 0
  end
  local x, z = 1200 * fraction, 800 * fraction + 100 * math.sin(fraction * math.pi)
  local hit = Physics.raycast(x, 6000, z, 0, -1, 0, 12000)
  if not hit then
    Profile.scope("Bench.flight.waiting_collision", function() end)
    return
  end
  self.elapsed = t
  local y = hit.point[2] + 3
  Profile.scope("Bench.flight." .. phase, function()
    Transform.set_position("camera", x, y, z)
    Transform.look_at("camera", x + math.sin(1.16), y + .02, z + math.cos(1.16))
    Hud.set_text("flight_position", phase ..
      string.format(" | X %.0f Z %.0f | 3 m above ground", x, z))
  end)
end
return Flight
