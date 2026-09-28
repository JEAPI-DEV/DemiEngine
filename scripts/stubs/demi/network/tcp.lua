---@meta
-- Native module: require("demi.network.tcp"). Annotations only.
-- Plain TCP is unencrypted and unframed. Define your own message boundaries;
-- use TLS or another authenticated protocol for sensitive traffic.

---@class TcpOptions
---@field timeout_ms? integer Nonnegative deadline in milliseconds; 0 disables it. Connect defaults to 5000, read/write to 30000.

---@class TcpResponse
---@field ok boolean True when the operation succeeded.
---@field data string Binary-safe bytes returned by read; empty for connect/write/failure.
---@field error_code string Stable failure category, empty on success (for example timeout, cancelled, eof, closed, read_pending, dns, connect, transport, shutdown).
---@field error string Sanitized failure description, empty on success.
---@field connection TcpConnection|nil Set on a successful connect only.

---@class TcpOperation
local Operation = {}
---@return boolean
function Operation:done() end
---Nil while pending. The result remains available after completion.
---@return TcpResponse|nil
function Operation:response() end
---Worker-only notification wait; omitted timeout waits indefinitely, 0 never blocks.
---Wait timeout returns nil,"timeout"; worker cancellation returns nil,"cancelled".
---Neither cancels the operation.
---Native failures still return TcpResponse; inspect ok and error.
---@param timeout_ms? integer Nonnegative milliseconds, at most 2147483647.
---@return TcpResponse|nil response
---@return string|nil error
function Operation:wait(timeout_ms) end
---Settles immediately; native cleanup follows on the reactor. An already
---submitted write may have reached the peer. Collection also cancels.
function Operation:cancel() end

---@class TcpConnection
local Connection = {}
---Returns an available chunk of at most max_bytes bytes. Reads do not wait to
---fill the requested size. Only one read may be pending; another completes
---with read_pending. Timeout/cancellation leaves buffered bytes available.
---@param max_bytes integer Positive maximum bytes to return.
---@param options? TcpOptions
---@return TcpOperation
function Connection:read(max_bytes, options) end
---Writes binary-safe bytes. A timeout closes the connection because delivery
---of already-submitted bytes cannot be determined.
---@param data string
---@param options? TcpOptions
---@return TcpOperation
function Connection:write(data, options) end
---Explicitly closes the socket and discards unread bytes. Close connections
---when finished; the service also closes them at host shutdown.
function Connection:close() end
---@return boolean
function Connection:open() end

---@class TcpService
local Tcp = {}
---Starts DNS and connection asynchronously. Invalid arguments return nil and
---a diagnostic. Transport failures appear in the operation response.
---@param host string
---@param port integer 1 through 65535.
---@param options? TcpOptions
---@return TcpOperation|nil operation
---@return string|nil error
function Tcp.connect(host, port, options) end

return Tcp
