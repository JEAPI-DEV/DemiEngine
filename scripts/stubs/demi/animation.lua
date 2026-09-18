---@meta
-- Native module: require("demi.animation"). Annotations only.
---@class AnimationService
local Animation = {}

---@class TwoBoneIkOptions2D
---@field root number[]
---@field target number[]
---@field pole number[]
---@field upper_length number
---@field lower_length number
---@class TwoBoneIkResult2D
---@field joint number[]
---@field end_position number[]
---@field reached boolean
---@param options TwoBoneIkOptions2D
---@return TwoBoneIkResult2D|nil
function Animation.solve_two_bone_2d(options) end

---@class TwoBoneIkOptions3D
---@field root number[]
---@field target number[]
---@field pole number[]
---@field upper_length number
---@field lower_length number
---@class TwoBoneIkResult3D
---@field joint number[]
---@field end_position number[]
---@field reached boolean
---@param options TwoBoneIkOptions3D
---@return TwoBoneIkResult3D|nil
function Animation.solve_two_bone_3d(options) end

---@class BoneSegment3D
---@field start number[] World-space bone head.
---@field tail number[] World-space bone tail.
---@field pole number[] World-space roll hint.
---@param entity_id string
---@param bone string
---@param segment BoneSegment3D
---@return boolean
function Animation.set_bone_segment(entity_id, bone, segment) end

---@param entity_id string
---@return boolean
function Animation.clear_bone_segments(entity_id) end

---Returns the active named state, or an empty string when unavailable.
---@param entity_id string
---@return string
function Animation.state(entity_id) end

---Immediately enters a named animation state.
---@param entity_id string
---@param state string
---@return boolean
function Animation.play(entity_id, state) end

---@param entity_id string
---@param parameter string
---@param value number
---@return boolean
function Animation.set_number(entity_id, parameter, value) end

---@param entity_id string
---@param parameter string
---@param value boolean
---@return boolean
function Animation.set_bool(entity_id, parameter, value) end

---@param entity_id string
---@param trigger string
---@return boolean
function Animation.trigger(entity_id, trigger) end

---@param entity_id string
---@param speed number
---@return boolean
function Animation.set_speed(entity_id, speed) end

---@param entity_id string
---@return number
function Animation.normalized_time(entity_id) end

---@class AnimationTransitionInfo
---@field from string
---@field to string
---@field progress number
---@field active boolean

---@param entity_id string
---@return AnimationTransitionInfo
function Animation.transition(entity_id) end

---@param entity_id string
---@param layer string
---@param weight number
---@return boolean
function Animation.set_layer_weight(entity_id, layer, weight) end

---@param entity_id string
---@param enabled boolean
---@return boolean
function Animation.set_root_motion(entity_id, enabled) end

return Animation
