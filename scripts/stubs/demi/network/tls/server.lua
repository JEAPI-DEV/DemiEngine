---@meta
-- Native module: require("demi.network.tls.server"). Annotations only.
---@class TlsEvent
---@field type "connected"|"disconnected"|"message"
---@field client_id integer
---@field message string

---@class TlsServerService
local TlsServer = {}
---@param port integer
---@param certificate string
---@param private_key string
---@param max_clients? integer
---@return boolean
function TlsServer.listen(port, certificate, private_key, max_clients) end
---@return TlsEvent[]
function TlsServer.events() end
---@param client_id integer
---@param message string
---@return boolean
function TlsServer.send(client_id, message) end
---@param client_id integer
function TlsServer.disconnect(client_id) end
---@param client_id integer
---@return boolean
function TlsServer.client_connected(client_id) end
---@return string
function TlsServer.error() end

return TlsServer
