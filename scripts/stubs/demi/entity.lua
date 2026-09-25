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
---@field position? Vec2|Vec3 Dense finite numeric array, flattened into local Transform2D/3D position.
---@field velocity? Vec2|Vec3 Dense finite numeric array, flattened into Rigidbody2D/3D; must match position dimensionality when both are supplied.
---@field components? table explicit component blocks; shorthand never overwrites these
---@param entity_id string
---@param options EntitySpawnOptions
---@return boolean ok
---@return string? error Nil on success; diagnostic on failure. Success queues creation.
---Rejects prefab and ttl options: use Prefab.instantiate and Timer.after.
---Explicit component fields override valid shorthand; coordinates must fit a float.
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
---Reads the last configured component values, not arbitrary live simulation
---state. Omitted fields can return nil. Use dedicated services such as
---Rigidbody3D.state for native-generated bodies and current physics state.
function Entity.get_config(entity_id, component, field) end
---@param entity_id string
---@param component string
---@param field string
---@param value any
---@return boolean
---Changes one validated field without replacing unrelated simulation state.
---Also updates the in-memory configuration returned by get_config; never saves
---the authored project file. Runtime read-only/restart-required fields reject edits.
function Entity.set_field(entity_id, component, field, value) end
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
---@return Vec2|Vec3|nil position Local position; nil without a supported transform.
function Entity.local_position(entity_id) end
---@param entity_id string
---@return Vec2|Vec3|nil position Hierarchy-resolved world position; nil when unavailable.
function Entity.world_position(entity_id) end

return Entity
