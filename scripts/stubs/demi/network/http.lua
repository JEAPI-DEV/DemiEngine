---@meta
-- Native module: require("demi.network.http"). Annotations only.

---@class HttpOptions
---@field url? string HTTP or HTTPS URL. Required for request(). Credentials in URLs are rejected.
---@field method? string HTTP method; defaults to GET. get()/post() select their own method.
---@field headers? table<string,string> Request headers, including Authorization when needed.
---@field body? string Binary-safe request body. Mutually exclusive with json.
---@field json? any JSON-serializable value; automatically adds application/json unless overridden.
---@field timeout_ms? integer Total timeout, default 30000 ms.
---@field connect_timeout_ms? integer Connection timeout, default 5000 ms.
---@field max_response_bytes? integer Response body limit, default 16 MiB; 0 disables the limit.
---@field max_header_bytes? integer Response header limit, default 64 KiB; 0 disables the limit.
---@field ca_certificate_pem? string Explicit PEM trust bundle. Certificate and hostname checks remain enabled.

---@class HttpResponse
---@field ok boolean True only when transport succeeded and status is 200–299.
---@field status integer Completed HTTP status, or 0 on a transport failure.
---@field body string Binary-safe response body after HTTP content decoding.
---@field headers table<string,string[]> Lowercase names with all duplicate values retained.
---@field error string Transport error description, empty for completed HTTP responses (including 4xx/5xx).
---@field error_code string Machine-readable transport error; empty on transport success.
---@field json any Parsed JSON value; JSON null uses Data.null. Nil for empty/invalid JSON.
---@field json_error string Empty for empty/valid JSON bodies; describes invalid JSON independently of transport.

---@class HttpRequestHandle
local Request = {}
---@return boolean
function Request:done() end
---Requests cancellation. Completion becomes observable asynchronously.
function Request:cancel() end
---Returns nil while pending. Completed results remain readable while the handle lives.
---@return HttpResponse|nil
function Request:response() end

---@class HttpService
local Http = {}
---Starts a request without blocking gameplay. Dropping the handle cancels it on collection.
---Redirects are returned as 3xx responses, not followed. Invalid options return nil and a diagnostic.
---@param options HttpOptions
---@return HttpRequestHandle|nil request
---@return string|nil error
function Http.request(options) end
---@param url string
---@param options? HttpOptions
---@return HttpRequestHandle|nil request
---@return string|nil error
function Http.get(url, options) end
---@param url string
---@param options? HttpOptions
---@return HttpRequestHandle|nil request
---@return string|nil error
function Http.post(url, options) end

return Http
