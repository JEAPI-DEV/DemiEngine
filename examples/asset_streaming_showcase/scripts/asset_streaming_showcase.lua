local AssetStreamingShowcase = {}

local GROUP_ID = "asset-group://optional_theme"
local ROOT_ASSET_ID = "asset://theme/accent"
local BADGE_ASSET_ID = "asset://theme/badge"

local function backend_summary(report)
  local entries = {}
  for _, asset in ipairs(report.assets) do
    entries[#entries + 1] = asset.asset_id .. " [" .. asset.backend .. "]"
  end
  if #entries == 0 then
    return "none"
  end
  return table.concat(entries, ", ")
end

function AssetStreamingShowcase:show_memory()
  local report = Assets.memory_report()
  Hud.set_text(
    "memory",
    "Resident: " .. report.resident_bytes .. " bytes\nResources: " .. backend_summary(report)
  )
end

function AssetStreamingShowcase:show_loaded_assets(is_visible)
  if is_visible then
    Hud.set_image("accent_preview", ROOT_ASSET_ID, 0, 0, 0, 0)
    Hud.set_image("badge_preview", BADGE_ASSET_ID, 0, 0, 0, 0)
  else
    Hud.set_image("accent_preview", "", 0, 0, 0, 0)
    Hud.set_image("badge_preview", "", 0, 0, 0, 0)
  end
  Hud.set_visible("accent_preview", is_visible)
  Hud.set_visible("badge_preview", is_visible)
end

function AssetStreamingShowcase:prepare_group()
  if self.request ~= nil then
    Assets.cancel(self.request)
  end
  if self.is_active then
    self:show_loaded_assets(false)
    Assets.release_group(GROUP_ID)
    self.is_active = false
  end

  local request, error = Assets.prepare_group(GROUP_ID)
  if request == 0 then
    Hud.set_text("stage", "Prepare failed: " .. error)
    return
  end
  self.request = request
  Hud.set_text("stage", "Stage: resolve")
end

function AssetStreamingShowcase:on_start()
  self.request = nil
  self.is_active = false
  self:show_memory()
  self:prepare_group()
end

function AssetStreamingShowcase:on_update(_dt)
  if self.request == nil then
    return
  end

  local progress = Assets.progress(self.request)
  Hud.set_text("stage", "Stage: " .. progress.stage)
  Hud.set_value("progress", progress.fraction)
  Hud.set_text(
    "counts",
    "Assets: " .. progress.completed_assets .. " / " .. progress.total_assets
  )

  if progress.stage == "ready" then
    local activated, error = Assets.activate(self.request)
    self.request = nil
    if not activated then
      Hud.set_text("stage", "Activation failed: " .. error)
      return
    end
    self.is_active = true
    self:show_loaded_assets(true)
    Hud.set_text("stage", "Stage: active")
    self:show_memory()
    Debug.log("Asset streaming showcase activated optional theme group")
  elseif progress.stage == "failed" or progress.stage == "cancelled" then
    self.request = nil
    Hud.set_text("stage", "Stage: " .. progress.stage .. " " .. progress.error)
    self:show_memory()
  end
end

function AssetStreamingShowcase:on_destroy()
  if self.request ~= nil then
    Assets.cancel(self.request)
    self.request = nil
  end
  if self.is_active then
    self:show_loaded_assets(false)
    Assets.release_group(GROUP_ID)
    self.is_active = false
  end
end

-- @HandleAction("prepare")
-- @HandleAction("cancel")
-- @HandleAction("reload")
-- @HandleAction("release")
function AssetStreamingShowcase:on_streaming_action(event)
  if event.action == "prepare" then
    self:prepare_group()
  elseif event.action == "cancel" and self.request ~= nil then
    Assets.cancel(self.request)
  elseif event.action == "reload" and self.is_active then
    local reloaded, error = Assets.reload(ROOT_ASSET_ID)
    Hud.set_text("stage", reloaded and "Stage: root reloaded" or "Reload failed: " .. error)
    self:show_memory()
  elseif event.action == "release" and self.is_active then
    self:show_loaded_assets(false)
    local released, error = Assets.release_group(GROUP_ID)
    if released then
      self.is_active = false
      Hud.set_value("progress", 0.0)
      Hud.set_text("stage", "Stage: released")
    else
      self:show_loaded_assets(true)
      Hud.set_text("stage", "Release failed: " .. error)
    end
    self:show_memory()
  end
end

return AssetStreamingShowcase
