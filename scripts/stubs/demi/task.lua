---@meta
-- Native module: require("demi.task"). Annotations only.

---@class WorkerTask
local Work = {}
---True after computation has stopped and its outcome is available.
---@return boolean
function Work:done() end
---@return string status queued, running, cancelling, completed, failed or cancelled.
function Work:status() end
---Returns copied function return values on success; otherwise nil and an error.
---@return any ...
function Work:result() end
---Nil while pending or after success.
---@return string|nil
function Work:error() end
---Requests cooperative cancellation. Running work must observe a cancellation checkpoint.
---@return boolean changed
function Work:cancel() end

---@class TaskService
local Task = {}
---Runs a Lua function on a pooled worker with its own VM. Arguments/results are
---copied. Primitive captures are snapshots; Shared.map captures remain shared.
---Ordinary table/function captures and engine userdata are rejected.
---@param work function
---@param ... any Copied data or explicit shared native handles.
---@return WorkerTask
function Task.fork(work, ...) end
---Configure pool size before its first job. Default: two reusable workers.
---@param options {workers:integer}
function Task.configure(options) end
---Worker cancellation query; returns false on the main VM. No instruction hooks are installed.
---@return boolean
function Task.cancelled() end
---Worker checkpoint; raises an error when cancelled. A no-op on the main VM.
function Task.check_cancelled() end
return Task
