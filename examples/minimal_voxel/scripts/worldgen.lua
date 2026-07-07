local chunks = require("worldgen.chunks")
local config = require("worldgen.config")
local inventory = require("worldgen.inventory")
local interaction = require("worldgen.interaction")
local performance = require("worldgen.performance")
local state = require("worldgen.state")
local terrain = require("worldgen.terrain")

local Worldgen = {}
local profile_enabled = os.getenv("DEMI_PROFILE") ~= nil and os.getenv("DEMI_PROFILE") ~= "0"

local function reset_state(self)
  self.loaded = {}
  self.loaded_count = 0
  self.loaded_section_count = 0
  self.column_heights = {}
  self.generated_heights = {}
  self.generated_biomes = {}
  self.generated_rocky_surfaces = {}
  self.generated_features = {}
  self.generated_feature_blocks = {}
  self.generated_feature_sections = {}
  self.block_overrides = {}
  self.desired = {}
  self.desired_sections = {}
  self.pending_loads = {}
  self.pending_unloads = {}
  self.pending_section_rebuilds = {}
  self.pending_load_head = 1
  self.pending_unload_head = 1
  self.pending_section_rebuild_head = 1
  self.queued_loads = {}
  self.queued_unloads = {}
  self.queued_section_rebuilds = {}
  self.dirty_sections = {}
  self.center_x = nil
  self.center_y = nil
  self.center_z = nil
  self.benchmark_x = 0.0
  self.performance = performance.create()
  self.inventory = inventory.create()
  self.left_mouse_down = false
  self.right_mouse_down = false
end

function Worldgen:on_start()
  reset_state(self)
  state.set(self)
  interaction.create_selection()
  inventory.create_hud()
  Hud.text("hud_chunks", "chunks: loading", 20.0, 116.0, 3.0)
  Hud.text("hud_target", "target: none", 20.0, 164.0, 3.0)
  Hud.text("hud_performance", "fps: -- avg: -- 1%: -- 0.1%: --", 20.0, 212.0, 3.0)
  self:on_update(0.0)
end

function Worldgen:on_update(dt)
  if profile_enabled then
    Profile.scope("Voxel.on_update", function()
      self:update_world(dt)
    end)
  else
    self:update_world(dt)
  end
end

function Worldgen:update_world(dt)
  if config.benchmark_enabled then
    self.benchmark_x = self.benchmark_x + config.benchmark_step
    Transform3D.set_position(config.camera_id, self.benchmark_x, 86.0, 44.0)
  end

  local x, y, z = Transform3D.get_position(config.camera_id)
  if x == nil or z == nil then
    return
  end

  local center_x = terrain.chunk_coord(x)
  local center_y = terrain.section_coord(y)
  local center_z = terrain.chunk_coord(z)
  if center_x ~= self.center_x or center_y ~= self.center_y or center_z ~= self.center_z then
    self.center_x = center_x
    self.center_y = center_y
    self.center_z = center_z
    if profile_enabled then
      Profile.scope("Voxel.chunks_sync", function()
        chunks.sync(self, center_x, center_y, center_z)
      end)
    else
      chunks.sync(self, center_x, center_y, center_z)
    end
  end

  if profile_enabled then
    Profile.scope("Voxel.chunks_process", function()
      chunks.process_queues(self)
    end)
  else
    chunks.process_queues(self)
  end

  local hit = nil
  if profile_enabled then
    Profile.scope("Voxel.interaction", function()
      inventory.update_input(self.inventory)
      if inventory.is_open(self.inventory) then
        interaction.set_selection_visible(nil)
      else
        hit = interaction.update(self)
      end
    end)
    Profile.scope("Voxel.hud", function()
      self:update_hud(hit, dt)
    end)
  else
    inventory.update_input(self.inventory)
    if inventory.is_open(self.inventory) then
      interaction.set_selection_visible(nil)
    else
      hit = interaction.update(self)
    end
    self:update_hud(hit, dt)
  end
end

function Worldgen:update_hud(hit, dt)
  Hud.set_text(
    "hud_chunks",
    string.format(
      "chunks: %d loaded, %d queued",
      self.loaded_section_count or 0,
      chunks.pending_count(self)
    )
  )
  Hud.set_text(
    "hud_target",
    hit and string.format("target: %d %d %d", hit.x, hit.y, hit.z) or "target: none"
  )
  Hud.set_text("hud_performance", performance.update(self.performance, dt))
  inventory.update_hud(self.inventory)
end

return Worldgen
