---@meta
-- Native module: require("demi.mesh.deformation"). Annotations only.
---@class MeshDentImpact
---@field point number[] World-space impact point (x, y, z).
---@field direction number[] World-space inward displacement direction.
---@field radius number Positive world-space brush radius in meters.
---@field depth number Positive dent depth in meters, at most radius / 2.

---@class MeshDeformationService
---Requires Dentable3D on the target entity; absent components reject requests.
local MeshDeformation = {}
---Apply a permanent visual dent to one static-model/procedural mesh instance.
---Does not modify collision, source assets, or other instances. Maximum 32 dents.
---@param entity_id string
---@param impact MeshDentImpact
---@return boolean accepted
---@return string error Empty when accepted.
function MeshDeformation.dent(entity_id, impact) end
---@param entity_id string
---@return boolean success
function MeshDeformation.reset(entity_id) end

---@class MeshImpactMaterial
---Optional per-call overrides of the target Dentable3D component's settings.
---@field radius? number Brush radius in meters (default 0.32).
---@field yield_energy? number Joules required before permanent damage (default 4).
---@field stiffness? number Effective stiffness in N/m (default 4000).
---@field absorption? number Absorbed fraction from 0 to 1 (default 0.7).
---@field max_depth? number Maximum dent depth in meters (default 0.15).
---@class MeshImpactContact
---@field point_x number
---@field point_y number
---@field point_z number
---@field normal_x number Body contact-force direction, not raycast outward normal.
---@field normal_y number
---@field normal_z number
---@field impact_energy number Pre-solver normal closing energy in joules, supplied by physics3d collision events.
---@param entity_id string
---@param contact MeshImpactContact
---@param material? MeshImpactMaterial
---@return boolean accepted
---@return string error
---@return number depth
function MeshDeformation.impact(entity_id, contact, material) end

return MeshDeformation
