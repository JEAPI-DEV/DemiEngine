---@meta
-- Native module: require("demi.network"). Annotations only.
---@class NetworkEvent
---@field type "connected"|"disconnected"|"message"
---@field peer_id integer
---@field channel integer
---@field message string
---@field latency_ms integer

---@class NetworkService
local Network = {}
---@return boolean
function Network.available() end
---@param port integer
---@param max_peers? integer
---@return boolean
function Network.host(port, max_peers) end
---@param port integer
---@param certificate string
---@param private_key string
---@param max_peers? integer
---@return boolean
function Network.host_dtls(port, certificate, private_key, max_peers) end
---@param address string
---@param port integer
---@return boolean
function Network.connect(address, port) end
---@param address string
---@param port integer
---@param trusted_certificate string
---@param server_name? string
---@return boolean
function Network.connect_dtls(address, port, trusted_certificate, server_name) end
function Network.disconnect() end
---@param peer_id integer
function Network.disconnect_peer(peer_id) end
---@param message string
---@param reliable? boolean
---@param peer_id? integer
---@param channel? integer
---@return boolean
function Network.send(message, reliable, peer_id, channel) end
---@return boolean
function Network.is_host() end
---@return boolean
function Network.is_connected() end
---@return boolean
function Network.is_secure() end
---@return string
function Network.security_error() end
---@return integer
function Network.latency_ms() end
---@return NetworkEvent[]
function Network.events() end
---@param assigned_peer_id? string
---@return string
function Network.sender_id(assigned_peer_id) end
---@param type string
---@param payload? table
---@return string
function Network.encode(type, payload) end
---@param message string
---@return table|nil
function Network.decode(message) end

return Network
