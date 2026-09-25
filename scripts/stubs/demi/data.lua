---@meta
-- Native module: require("demi.data"). Annotations only.
---@class ScriptPropertyDefinition
---@field type 'boolean'|'number'|'integer'|'string'|'asset'|'entity'|'enum'|'array'|'object'|'vec2'|'vec3'|'color'
---@field default? any Applied before on_create when no override is authored.
---@field required? boolean Requires an authored value when no default exists.
---@field minimum? number
---@field maximum? number
---@field values? string[] Required for enum properties.

---@alias ScriptPropertySchema table<string, ScriptPropertyDefinition>

---@class DataError
---@field code string
---@field message string
---@field path string Asset ID or diagnostic source path.

---@class DataQuery
---@field content_type? string
---@field tags? string[]

---@class DataService
---@field null table JSON null sentinel; distinguish it with `Data.is_null`.
local Data = {}
---@param id string Stable `asset://` ID.
---@return any? snapshot A detached Lua snapshot of the immutable document.
---@return DataError? error
function Data.load(id) end
---@param text string YAML source text.
---@param source? string Diagnostic source label.
---@return any? value
---@return DataError? error
function Data.parse_yaml(text, source) end
---@param query? DataQuery
---@return any[] snapshots Document roots (including scalars/null sentinels), ordered deterministically by stable asset ID.
function Data.query(query) end
---@param id string
---@return integer
function Data.revision(id) end
---@param value any
---@return "array"|"object"|"null"|nil
function Data.kind(value) end
---@param value any
---@return boolean
function Data.is_null(value) end

return Data
