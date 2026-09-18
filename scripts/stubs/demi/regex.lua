---@meta
-- Native module: require("demi.regex"). Annotations only.
---@class RegexService
local Regex = {}
---@param pattern string
---@return boolean
function Regex.is_valid(pattern) end
---@param value string
---@param pattern string ECMAScript regular expression
---@param case_sensitive? boolean Defaults to false
---@return boolean
function Regex.matches(value, pattern, case_sensitive) end

return Regex
