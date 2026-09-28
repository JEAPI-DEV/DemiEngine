---@meta
-- Native module: require("demi.database"). Annotations only.

---@class DatabaseOptions
---@field driver? string Registered driver name; defaults to sqlite.
---@field path? string SQLite file relative to platform user data/databases, or ':memory:'. Other drivers receive this endpoint unchanged.
---@field options? table<string, any> Driver options; SQLite supports busy_timeout_ms.

---@class DatabaseStatement
---@field sql string SQL text with placeholders.
---@field parameters? any[] Positional values; use Data.null for SQL NULL and Database.blob for SQL BLOB.
---@field query? boolean True to return rows from this statement.

---@class DatabaseBlob
---Opaque bound BLOB parameter created by Database.blob().

---@class DatabaseQueryResult
---@field columns string[] Column names in result order, including duplicates.
---@field rows any[][] Row values in column order; SQL NULL uses Data.null and BLOB uses a binary safe Lua string.

---@class DatabaseExecuteResult
---@field changes integer Affected row count.
---@field last_insert_id integer Last inserted row ID for SQLite.

---@class DatabaseOperation
local Operation = {}
---@return boolean
function Operation:done() end
---Returns nil while pending or after failure. Check error() after done().
---@return DatabaseQueryResult|DatabaseExecuteResult|table|nil
function Operation:result() end
---Worker only. Waits cooperatively and returns the decoded result or nil,error.
---A timeout stops waiting without cancelling the operation. Omit it to wait until completion or task cancellation.
---@param timeout_ms? integer Non-negative milliseconds; zero checks immediately.
---@return DatabaseQueryResult|DatabaseExecuteResult|table|nil result
---@return string|nil error
function Operation:wait(timeout_ms) end
---Empty while pending or after success.
---@return string
function Operation:error() end
function Operation:cancel() end

---@class DatabaseConnection
local Connection = {}
---@return DatabaseOperation
function Connection:connect_operation() end
---@param sql string SQL with positional placeholders; values are bound, never interpolated.
---@param parameters? any[] Plain strings bind as TEXT; Database.blob values bind as BLOB.
---@return DatabaseOperation
function Connection:query(sql, parameters) end
---@param sql string
---@param parameters? any[] Plain strings bind as TEXT; Database.blob values bind as BLOB.
---@return DatabaseOperation
function Connection:execute(sql, parameters) end
---@param statements DatabaseStatement[]
---@return DatabaseOperation
function Connection:transaction(statements) end
---@return DatabaseOperation
function Connection:close() end

---@class DatabaseService
local Database = {}
---Marks bytes for SQL BLOB binding. Plain strings bind as SQL TEXT.
---@param bytes string Binary safe Lua string, including empty strings and NUL bytes.
---@return DatabaseBlob
function Database.blob(bytes) end
---Connection setup runs on a native worker. Poll connect_operation():done() before inspecting its result.
---@param settings DatabaseOptions
---@return DatabaseConnection|nil connection
---@return string|nil error
function Database.connect(settings) end
---Worker only. Connects and waits for setup, returning a ready connection or nil,error.
---Use db:query(sql, parameters):wait() for sequential worker code.
---@param settings DatabaseOptions
---@return DatabaseConnection|nil connection
---@return string|nil error
function Database.open(settings) end

return Database
