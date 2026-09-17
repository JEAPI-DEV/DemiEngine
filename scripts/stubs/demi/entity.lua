---@meta
-- Native module: require("demi.entity"). Annotations only.
---@class EntityService
local Entity = {}
---@class EntityQuery
---@field all? string[]
---@field tags? string[]
---@field layer? string
---@field include_disabled? boolean
---@param id_or_name string
---@return string|nil
function Entity.find(id_or_name) end
---@param entity_id string
---@return boolean
function Entity.exists(entity_id) end
---@param entity_id string
---@param spec table
---@return boolean
function Entity.create(entity_id, spec) end
---@class EntitySpawnOptions
---@field prefab? string prefab:// reference expanded by RuntimeObjectModel
---@field position? number[] [x,y] or [x,y,z] flattened into Transform2D/3D
---@field velocity? number[] flattened into Rigidbody2D/3D
---@field ttl? number seconds until auto-destroy via Timer.after (use Script.after or Entity.spawn_ttl)
---@field components? table explicit component blocks; shorthand never overwrites these
---@param entity_id string
---@param options EntitySpawnOptions
---@return boolean ok
---@return integer timer_id ttl timer when ttl > 0, else 0
function Entity.spawn(entity_id, options) end
---@param entity_id string
---@param spec table
---@return boolean
function Entity.replace(entity_id, spec) end
---@param source_id string
---@param new_id string
---@return boolean
function Entity.clone(source_id, new_id) end
---@param entity_id string
---@return boolean
function Entity.destroy(entity_id) end
---@param entity_ids string[]
---@return integer
function Entity.destroy_many(entity_ids) end
---@param entity_id string
---@param enabled boolean
---@return boolean
function Entity.set_enabled(entity_id, enabled) end
---@param entity_id string
---@return boolean
function Entity.is_enabled(entity_id) end
---@param entity_id string
---@param component string
---@param values table
---@return boolean
function Entity.add_component(entity_id, component, values) end
---@param entity_id string
---@param component string
---@return boolean
function Entity.remove_component(entity_id, component) end
---@param entity_id string
---@param component string
---@return boolean
function Entity.has_component(entity_id, component) end
---@param entity_id string
---@param component string
---@param field string
---@return any
function Entity.get(entity_id, component, field) end
---@param entity_id string
---@param component string
---@param field string
---@param value any
---@return boolean
function Entity.set(entity_id, component, field, value) end
---@param query EntityQuery
---@return string[]
function Entity.query(query) end
---@param entity_id string
---@param parent_id? string
---@return boolean
function Entity.set_parent(entity_id, parent_id) end
---@param entity_id string
---@return string|nil
function Entity.parent(entity_id) end
---@param entity_id string
---@return string[]
function Entity.children(entity_id) end
---@param entity_id string
---@return number[]|nil
function Entity.local_position(entity_id) end
---@param entity_id string
---@return number[]|nil
function Entity.world_position(entity_id) end

return Entity
