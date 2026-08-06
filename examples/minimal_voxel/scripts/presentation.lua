local Presentation = {}

local function lerp(a, b, t)
  return a + (b - a) * t
end

local function color(a, b, t)
  return {
    lerp(a[1], b[1], t),
    lerp(a[2], b[2], t),
    lerp(a[3], b[3], t),
    1.0,
  }
end

function Presentation:on_create()
  self.cycle_seconds = 150.0
end

function Presentation:on_update(_dt)
  local phase = (Time.time % self.cycle_seconds) / self.cycle_seconds
  local angle = phase * math.pi * 2.0
  local daylight = math.max(0.08, math.sin(angle) * 0.5 + 0.5)
  local dusk = 1.0 - math.abs(daylight * 2.0 - 1.0)
  local sky = color({ 0.035, 0.06, 0.13 }, { 0.38, 0.58, 0.82 }, daylight)
  local warm = color(sky, { 0.95, 0.34, 0.12 }, dusk * 0.32)

  Entity.set("ent_sun", "DirectionalLight", "direction", {
    math.cos(angle) * 0.55,
    -math.max(0.12, daylight),
    math.sin(angle) * 0.45,
  })
  Entity.set("ent_sun", "DirectionalLight", "intensity", 0.18 + daylight * 1.15)
  Entity.set("ent_sun", "DirectionalLight", "color", color(
    { 0.34, 0.45, 0.72 },
    { 1.0, 0.96, 0.84 },
    daylight
  ))
  Entity.set("ent_environment", "Environment3D", "ambient_color", warm)
  Entity.set("ent_environment", "Environment3D", "ambient_intensity", 0.22 + daylight * 0.48)
  Entity.set("ent_environment", "Environment3D", "fog_color", warm)
  Entity.set("ent_camera", "Camera3D", "clear_color", warm)
end

return Presentation
