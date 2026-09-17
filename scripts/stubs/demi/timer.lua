---@meta
-- Native module: require("demi.timer"). Annotations only.
---@class TimerService
local Timer = {}
---@param seconds number
---@param callback fun(timer_id: integer)
---@return integer timer_id
function Timer.delay(seconds, callback) end
---@param seconds number One-shot timer; intent-revealing alias of delay. Bind lifetime via Script.after.
---@param callback fun(timer_id: integer)
---@return integer timer_id
function Timer.after(seconds, callback) end
---@param seconds number
---@param callback fun(timer_id: integer)
---@return integer timer_id
function Timer.every(seconds, callback) end
---@param timer_id integer
---@return boolean
function Timer.cancel(timer_id) end

return Timer
