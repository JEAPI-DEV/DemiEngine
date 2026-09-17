---@meta
-- Native module: require("demi.crypto"). Annotations only.
---@class CryptoService
local Crypto = {}
---@param bytes? integer
---@return string
function Crypto.random_token(bytes) end
---@param password string
---@param salt string
---@param iterations? integer
---@return string
function Crypto.password_hash(password, salt, iterations) end
---@param left string
---@param right string
---@return boolean
function Crypto.secure_equals(left, right) end

return Crypto
