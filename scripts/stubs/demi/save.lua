---@meta
-- Native module: require("demi.save"). Annotations only.
---@class SaveService
local Save = {}
---@param slot string
---@param key string
---@param fallback? number
---@return number
function Save.get_number(slot, key, fallback) end
---@param slot string
---@param key string
---@param value number
---@return boolean
function Save.set_number(slot, key, value) end
---@param slot string
---@param key string
---@param fallback? string
---@return string
function Save.get_string(slot, key, fallback) end
---@param slot string
---@param key string
---@param value string
---@return boolean
function Save.set_string(slot, key, value) end
---@param slot string
---@return table|nil
function Save.read(slot) end
---@param slot string
---@param state table
---@param format_version? integer
---@return boolean
function Save.write(slot, state, format_version) end

---@class GameSaveState
---@field game table<string, any>
---@field selected_entities table<string, any>
---@field prefab_instances table<string, any>
---@field lua table<string, any>

---@class GameSaveOptions
---@field format_version? integer
---@field autosave? boolean
---@field sequence? integer
---@field reason? string

---@class GameSaveMetadata
---@field autosave boolean
---@field sequence integer
---@field reason string

---@param slot string
---@param state GameSaveState
---@param options? GameSaveOptions
---@return boolean
function Save.write_state(slot, state, options) end

---@param slot string
---@return GameSaveState?
function Save.read_state(slot) end

---@param slot string
---@return GameSaveMetadata?
function Save.metadata(slot) end

---@return string
function Save.last_error() end
---@param slot string
---@return boolean
function Save.exists(slot) end

---@param from_version integer
---@param to_version integer
---@param callback fun(state: table, from_version: integer, to_version: integer): table
---@return integer migration_id
function Save.register_migration(from_version, to_version, callback) end

---@param slot string
---@return integer
function Save.version(slot) end

---@param slot string
---@return boolean
function Save.delete(slot) end

return Save
