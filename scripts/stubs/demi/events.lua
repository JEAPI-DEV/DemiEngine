---@meta
-- Native module: require("demi.events"). Annotations only.
---@class EventsService
local Events = {}
---@param event_name string
---@param callback fun(payload: table)
---@return integer subscription_id
function Events.subscribe(event_name, callback) end
---@param subscription_id integer
---@return boolean
function Events.unsubscribe(subscription_id) end
---@param event_name string
---@param payload? table
---@return integer delivered
function Events.emit(event_name, payload) end

return Events
