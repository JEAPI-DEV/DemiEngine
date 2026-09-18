---@meta
-- Native module: require("demi.physics.destruction3d"). Annotations only.
---@class Destruction3DState
---@field root string
---@field status string unattached, ready, queued, applied, or failed
---@field error string
---@field revision integer
---@field bodies integer
---@field parts table<string,string> Collider part ID to its current physical entity ID.
---@class Destruction3DImpact
---@field position number[] Required world-space [x,y,z].
---@field radius? number World-space metres, 0.001..1000; defaults to 0.5.
---@field energy? number Total fracture budget in joules, 0..1e12; defaults to 0.
---@field impulse? number Total momentum budget in N*s, 0..1e9; defaults to 0.
---@field direction? number[] Normalized internally; omitted/zero means radial. Coincident radial points use +Y.
---@field entity? string Optional root/current-fragment filter. Omit to affect nearby assemblies.
---@class Destruction3DService
---Destructible3D assemblies and Fracture3D mesh components generate mappings and ModelCollider3D.inline_geometry;
---gameplay should not maintain these generated collider payloads manually.
local Destruction3D = {}
---Uses actual collider parts and linear surface-distance falloff. Budgets are
---shared, not multiplied per fragment. energy_per_health converts joules to
---authored bond-health units. Impulses apply at contacts after successful commit.
---Requires the first physics step; returns queued acceptance, not completion.
---At least one of energy/impulse must be positive. Queue limit: 512 part impulses
---per assembly. No occlusion/shielding or ordinary non-destructible body blast.
---@param options Destruction3DImpact
---@return boolean accepted
---@return string error
---@return integer affected_assemblies
function Destruction3D.impact(options) end
---Queues damage to incident bonds and the selected part's foundation attachment.
---Foundation health is the strongest incident authored bond health (1 for an isolated part).
---Requires Destructible3D; attaches after the first physics step. Acceptance
---does not guarantee commit: poll state for a body-budget/preparation failure.
---@param entity_id string Assembly root or a current fragment entity.
---@param part_id string Stable collider part ID.
---@param damage number Positive finite damage in authored bond-health units.
---@return boolean accepted
---@return string error
function Destruction3D.damage_part(entity_id, part_id, damage) end
---@param entity_id string
---@return Destruction3DState
function Destruction3D.state(entity_id) end

return Destruction3D
