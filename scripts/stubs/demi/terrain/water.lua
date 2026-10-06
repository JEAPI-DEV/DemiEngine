---@meta
-- Native module: require("demi.terrain.water"). Annotations only.

---@class WaterSample
---@field terrain_id string Stable entity ID of the terrain placement.
---@field body_id string Stable water-body ID within that terrain.
---@field kind "lake"|"ocean"|"river"
---@field surface Vec3 World-space surface point projected along terrain-local up, not necessarily global vertical.
---@field normal Vec3 World-space unit surface normal.
---@field depth number Bed-to-surface depth in world units along terrain-local up.
---@field underwater boolean The queried point lies at or above the bed and below the surface.

---@class WaterEvent
---@field phase "enter"|"exit"|"stay"
---@field sample WaterSample

---Opaque native tracker for point immersion, not collider extents.
---Scripts choose cadence by calling update in on_fixed_update or on_update;
---there are no interrupt callbacks or automatic global listeners.
---@class WaterTracker
local WaterTracker = {}

---Samples a world-space point and returns an ordered event array.
---First immersion emits enter; the same body emits stay on every explicit update.
---Switching bodies emits exit for the old body, then enter for the new body.
---Outside water emits exit once, then no events until immersion resumes.
---Terrain unload/disable and regeneration are reflected on the next update.
---Identity includes terrain placement, body and scene; scene identity is private.
---@param position Vec3 Dense one-based array of exactly three finite numbers: {x, y, z}.
---@return WaterEvent[]
function WaterTracker:update(position) end

---Returns the immersed sample retained by the latest update, or nil.
---@return WaterSample|nil
function WaterTracker:current() end

---Clears retained immersion without emitting an event.
function WaterTracker:reset() end

---@class TerrainWaterService
local TerrainWater = {}

---Queries prepared water data without a renderer dependency or terrain generation.
---Follows translated, rotated and scaled terrain placements.
---A wet column can return a sample with underwater=false; dry or unavailable
---water returns nil. Points on the surface or below the bed are not immersed.
---@param position Vec3 World-space dense one-based array of exactly three finite numbers: {x, y, z}.
---@param terrain_id? string Optional terrain placement entity ID; omit to query loaded, enabled terrains.
---@return WaterSample|nil
function TerrainWater.sample(position, terrain_id) end

---Creates an opaque point-immersion tracker with the optional terrain filter.
---Gameplay mechanics such as swimming and buoyancy remain script-owned;
---automatic buoyancy and animated waves are not implemented.
---@param terrain_id? string Optional terrain placement entity ID.
---@return WaterTracker
function TerrainWater.tracker(terrain_id) end

return TerrainWater
