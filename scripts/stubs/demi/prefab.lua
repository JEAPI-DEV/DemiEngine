---@meta
-- Native module: require("demi.prefab"). Annotations only.
---@class PrefabInstantiateOptions
---@field id string
---@field position? number[]
---@field overrides? table<string, table>
---@field pooled? boolean

---@class PrefabService
local Prefab = {}
---@param prefab_id string
---@param options PrefabInstantiateOptions
---@return string|nil instance_id
function Prefab.instantiate(prefab_id, options) end
---@param instance_or_entity_id string
---@return boolean
function Prefab.release(instance_or_entity_id) end
---@param prefab_id string
---@return integer
function Prefab.pooled_count(prefab_id) end

return Prefab
