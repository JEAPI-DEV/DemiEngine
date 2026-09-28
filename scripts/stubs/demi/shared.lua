---@meta
-- Native module: require("demi.shared"). Annotations only.

---@class SharedMap
local Map = {}
---Returns an independent copy, or nil for a missing key. Busy/cancelled access
---returns nil, error. Keys are strings, including binary strings.
---@param key string
---@return any value
---@return string|nil error
function Map:get(key) end
---Stores a copied transfer graph. Functions, threads, unsupported userdata and
---shared handles (including nested handles) are rejected. Ordinary value tables
---may contain cycles; supported JSON array/object/NULL markers are preserved.
---@param key string
---@param value any
---@return boolean|nil success
---@return string|nil error
function Map:set(key, value) end
---Atomically adds a finite number. A missing key starts at zero. Non-numeric
---values and overflow return nil, error without changing the stored value.
---@param key string
---@param delta number
---@return number|nil value
---@return string|nil error
function Map:add(key, delta) end
---Copies the current entries into a regular Lua table. Stored nil values appear
---as absent keys in this table, following normal Lua table semantics.
---@return table<string, any>|nil values
---@return string|nil error
function Map:snapshot() end
---Acquires the map lock, calls fn(map), then automatically unlocks. No manual
---lock()/unlock() is needed. Use this to keep several reads/writes together.
---Returns true followed
---by all callback results (including nils). Callback errors are rethrown after
---unlocking. Yielding is rejected and also releases the lock. Reentering the
---same map is allowed. For multiple maps, always acquire locks in the same
---application-defined order on every worker to avoid deadlock.
---On the main VM every method only tries the lock: contention returns busy.
---Workers wait with cancellation checks. Keep locked callbacks short.
---@param fn fun(map: SharedMap): ...
---@return boolean success
---@return any ... Callback results, or 'busy'/'cancelled' on failure.
function Map:with_lock(fn) end
---Tries to acquire the lock without waiting, even on workers. If acquired,
---calls fn(map) and automatically unlocks afterward, including on callback errors.
---Contention returns false,'busy' without running fn.
---@param fn fun(map: SharedMap): ...
---@return boolean success
---@return any ...
function Map:try_lock(fn) end

---@class SharedService
local Shared = {}
---Creates explicitly shared native storage. Initial keys must be strings;
---values obey the same copied-graph contract as set. Passing this handle across
---VMs shares this native object; get and snapshot always copy its values.
---@param initial? table<string, any>
---@return SharedMap|nil map
---@return string|nil error
function Shared.map(initial) end
return Shared
