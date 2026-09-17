---@meta
-- Native module: require("demi.sprite2d"). Annotations only.
---@class Sprite2DService
local Sprite2D = {}
---@param entity_id string
---@param r number
---@param g number
---@param b number
---@param a? number
---@return boolean
function Sprite2D.set_color(entity_id, r, g, b, a) end
---@param entity_id string
---@param clip string
---@param restart? boolean
---@return boolean
function Sprite2D.play_animation(entity_id, clip, restart) end
---@param entity_id string
---@return boolean
function Sprite2D.pause_animation(entity_id) end
---@param entity_id string
---@return boolean
function Sprite2D.resume_animation(entity_id) end
---@param entity_id string
---@return string
function Sprite2D.current_animation(entity_id) end
---@param entity_id string
---@param flip_x boolean
---@param flip_y boolean
---@return boolean
function Sprite2D.set_flip(entity_id, flip_x, flip_y) end
---@param entity_id string
---@param width number
---@param height number
---@return boolean
function Sprite2D.set_size(entity_id, width, height) end
---@param entity_id string
---@param layer string
---@return boolean
function Sprite2D.set_layer(entity_id, layer) end
---@param entity_id string
---@param sorting_order integer
---@return boolean
function Sprite2D.set_sorting_order(entity_id, sorting_order) end
---@param entity_id string
---@param material string
---@return boolean
function Sprite2D.set_material(entity_id, material) end

return Sprite2D
