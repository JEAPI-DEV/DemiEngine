local chunks = require("worldgen.chunks")
local config = require("worldgen.config")
local interaction = require("worldgen.interaction")
local terrain = require("worldgen.terrain")

local Worldgen = {}

local function reset_state(self)
  self.loaded = {}
  self.column_heights = {}
  self.desired = {}
  self.pending_loads = {}
  self.pending_unloads = {}
  self.queued_loads = {}
  self.queued_unloads = {}
  self.center_x = nil
  self.center_z = nil
  self.benchmark_x = 0.0
  self.left_mouse_down = false
  self.right_mouse_down = false
end

function Worldgen:on_start()
  reset_state(self)
  interaction.create_selection()
  Hud.text("hud_chunks", "chunks: loading", 20.0, 116.0, 3.0)
  Hud.text("hud_target", "target: none", 20.0, 164.0, 3.0)
  self:on_update(0.0)
end

function Worldgen:on_update(_dt)
  if config.benchmark_enabled then
    self.benchmark_x = self.benchmark_x + config.benchmark_step
    Transform3D.set_position(config.camera_id, self.benchmark_x, 86.0, 44.0)
  end

  local x, _y, z = Transform3D.get_position(config.camera_id)
  if x == nil or z == nil then
    return
  end

  local center_x = terrain.chunk_coord(x)
  local center_z = terrain.chunk_coord(z)
  if center_x ~= self.center_x or center_z ~= self.center_z then
    self.center_x = center_x
    self.center_z = center_z
    chunks.sync(self, center_x, center_z)
  end

  chunks.process_queues(self)
  local hit = interaction.update(self)

  Hud.set_text(
    "hud_chunks",
    string.format("chunks: %d loaded, %d queued", terrain.table_count(self.loaded), #self.pending_loads + #self.pending_unloads)
  )
  Hud.set_text(
    "hud_target",
    hit and string.format("target: %d %d %d", hit.x, hit.y, hit.z) or "target: none"
  )
end

return Worldgen
