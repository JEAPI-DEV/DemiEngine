---@meta
-- Native module: require("demi.scene"). Annotations only.
---@class SceneService
local Scene = {}
---@param scene_id string
---@return boolean
function Scene.load(scene_id) end
---@return boolean
function Scene.reload() end
---@param scene_id string
---@param additive? boolean
---@return boolean
function Scene.prepare(scene_id, additive) end
---@return boolean
function Scene.cancel() end
---@return number
function Scene.progress() end
---@return boolean
function Scene.is_prepared() end
---@return boolean
function Scene.activate() end
---@param scene_id string
---@return boolean
function Scene.unload(scene_id) end
---@param entity_id string
---@param persistent boolean
---@return boolean
function Scene.set_persistent(entity_id, persistent) end
---@return string
function Scene.active() end
---@return string
function Scene.error() end

return Scene
