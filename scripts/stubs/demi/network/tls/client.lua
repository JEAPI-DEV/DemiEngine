---@meta
-- Native module: require("demi.network.tls.client"). Annotations only.
---@class TlsClientService
local TlsClient = {}
---@param host string
---@param port integer
---@param trusted_certificate string
---@param server_name? string
---@return boolean
function TlsClient.connect(host, port, trusted_certificate, server_name) end
---@return TlsEvent[]
function TlsClient.events() end
---@param message string
---@return boolean
function TlsClient.send(message) end
function TlsClient.disconnect() end
---@return boolean
function TlsClient.is_connected() end
---@return string
function TlsClient.error() end

return TlsClient
