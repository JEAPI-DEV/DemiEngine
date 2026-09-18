---@meta
-- Native module: require("demi.assets"). Annotations only.
---@class AssetGroupProgress
---@field stage "resolve"|"read"|"decode"|"upload"|"ready"|"failed"|"cancelled"
---@field fraction number
---@field completed_assets integer
---@field total_assets integer
---@field pending_bytes integer
---@field decoded_bytes integer
---@field resident_bytes integer
---@field error string

---@class AssetMemoryEntry
---@field asset_id string
---@field backend string
---@field resident_bytes integer
---@field owners string[]

---@class AssetMemoryReport
---@field pending_bytes integer
---@field decoded_bytes integer
---@field resident_bytes integer
---@field assets AssetMemoryEntry[]

---@class AssetsService
local Assets = {}
---@param uri string asset:// resource or asset-group:// batch
---@return integer request
---@return string error
function Assets.load(uri) end
---@param request integer
---@return AssetGroupProgress
function Assets.progress(request) end
---@param request integer
---@return boolean
function Assets.is_ready(request) end
---@param request integer
---@return boolean
function Assets.cancel(request) end
---@param uri string asset:// resource or asset-group:// batch
---@return boolean success
---@return string error
function Assets.unload(uri) end
---@param asset_id string
---@return boolean success
---@return string error
function Assets.reload(asset_id) end
---@param asset_id string Loaded single-source asset ID.
---@return string? text Cached resident source; never rereads the file.
---@return string error
function Assets.text(asset_id) end
---@return AssetMemoryReport
function Assets.memory_report() end

return Assets
