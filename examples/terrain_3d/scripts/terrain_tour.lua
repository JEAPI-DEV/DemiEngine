local Path = require("demi.camera.path")
local Transform3D = require("demi.transform3d")
local Input = require("demi.input")
local Application = require("demi.application")
local Hud = require("demi.hud")
local Route = require("terrain_route")

---@demi_component
---@display_name Terrain camera tour
---@category Showcase
local TerrainTour = {}

---@demi_property
---@range 2 30
TerrainTour.chapter_seconds = 10.0

function TerrainTour:apply_pose(pose)
  local position = pose.position
  local target = pose.target
  Transform3D.set_position("camera", position[1], position[2], position[3])
  Transform3D.look_at("camera", target[1], target[2], target[3])
  if self.chapter ~= pose.segment then
    self.chapter = pose.segment
    local chapter = Route[pose.segment]
    Hud.set_text("tour_chapter", string.format("%02d / %02d   %s", pose.segment, #Route, chapter.name))
    Hud.set_text("tour_description", chapter.description)
  end
  Hud.set_value("tour_progress", pose.progress)
end

function TerrainTour:refresh_controls()
  Hud.set_text("tour_pause", self.path.playing and "Pause tour" or "Resume tour")
  Hud.set_text("tour_status", self.path.playing and "CAMERA TOUR / LOOPING" or "PAUSED")
end

function TerrainTour:on_start()
  self.path = Path.new(Route, {duration = self.chapter_seconds * #Route, loop = true})
  self.chapter = nil
  self:apply_pose(self.path:sample(0))
  self:refresh_controls()
end

-- @HandleAction("tour_pause")
function TerrainTour:on_pause()
  if self.path.playing then
    self.path:pause()
  else
    self.path:play()
  end
  self:refresh_controls()
end

-- @HandleAction("tour_restart")
function TerrainTour:on_restart()
  self:apply_pose(self.path:restart())
  self:refresh_controls()
end

-- @HandleAction("tour_next")
function TerrainTour:on_next()
  local chapter = math.floor(self.path.elapsed / self.chapter_seconds) + 1
  local elapsed = (chapter % #Route) * self.chapter_seconds
  self:apply_pose(self.path:seek(elapsed))
end

function TerrainTour:on_update(dt)
  if Input.key_pressed("escape") then
    Application.quit()
    return
  end
  if Input.key_pressed("space") then
    self:on_pause()
  end
  if Input.key_pressed("r") then
    self:on_restart()
  end
  if Input.key_pressed("n") then
    self:on_next()
  end
  if self.path.playing then
    self:apply_pose(self.path:update(dt))
  end
end

return TerrainTour
