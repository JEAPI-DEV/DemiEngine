---@meta
-- Native module: require("demi.cutscene"). Annotations only.
---@class CutsceneService
local Cutscene = {}
---@param id string
---@return boolean
function Cutscene.play(id) end
---@return boolean
function Cutscene.pause() end
---@return boolean
function Cutscene.resume() end
---@return boolean
function Cutscene.skip() end
---@return boolean
function Cutscene.stop() end
---@return boolean
function Cutscene.is_playing() end
---@return string
function Cutscene.active() end

return Cutscene
