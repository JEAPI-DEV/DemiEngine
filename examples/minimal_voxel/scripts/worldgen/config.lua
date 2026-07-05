local pack_loaded, pack = pcall(require, "generated.pack_import")
if not pack_loaded then
  pack = {
    texture = "",
    atlas_columns = 4,
    blocks = {
      [1] = { top = 0, side = 1, bottom = 2 },
      [2] = { top = 2, side = 2, bottom = 2 },
      [3] = { top = 3, side = 3, bottom = 3 },
    },
  }
end

return {
  pack = pack,
  chunk_size = 16,
  chunk_height = 255,
  load_radius = 3,
  unload_radius = 4,
  camera_id = "ent_camera",
  selection_id = "ent_block_selection",
  benchmark_enabled = os.getenv("DEMI_VOXEL_BENCH") == "1",
  benchmark_step = 1.0,
  chunk_load_budget = tonumber(os.getenv("DEMI_VOXEL_CHUNK_LOADS_PER_FRAME") or "") or 1,
  chunk_unload_budget = tonumber(os.getenv("DEMI_VOXEL_CHUNK_UNLOADS_PER_FRAME") or "") or 16,
  interaction_range = 8.0,
  ray_step = 0.12,
  terrain = {
    seed = 4242,
    base_height = 64,
    amplitude = 16,
    frequency = 0.026,
    fill_depth = 5,
    cap_block = 1,
    fill_block = 2,
    base_block = 3,
  },
}
