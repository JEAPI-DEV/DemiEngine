---@meta
-- Native module: require("demi.prefab"). Annotations only.
---@class PrefabInstantiateOptions
---@field id string
---@field position? Vec2|Vec3
---@field overrides? table<string, table>
---@field pooled? boolean

---@class PrefabService
local Prefab = {}
---@class PrefabPlacement3D
---@field id string
---@field prefab string
---@field root string
---@field position Vec3 World-space placement position.
---@field rotation Vec3 World-space Euler rotation (radians).
---@field scale Vec3 World-space scale.
---@field preserve boolean
---@param ancestor? string Limit to this entity and its descendants.
---@return PrefabPlacement3D[] placements Enabled authored markers, sorted by ID; does not spawn anything.
function Prefab.placements(ancestor) end
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
---@param entries integer Nonnegative retained-template count; zero disables and clears the cache.
---@return boolean accepted
---Controls preparation-cache retention, not live instance or entity limits. Default: 16.
function Prefab.set_template_cache_capacity(entries) end

return Prefab
