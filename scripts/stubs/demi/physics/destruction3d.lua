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
---Fracture3D.density is authored in kg/m^3; generated bodies preserve weighted
---mass/inertia. An explicit assembly Rigidbody3D.mass overrides the total.
---Fracture3D.collider="box" with pieces=1 preserves a detailed unit-box visual
---and uses a simple size-scaled collision proxy; it does not slice that visual.
local Destruction3D = {}
---Uses actual collider parts and linear surface-distance falloff. Budgets are
---shared, not multiplied per fragment. energy_per_health converts joules to
---authored bond-health units. Impulses apply at contacts after successful commit.
---Requires the first physics step; returns queued acceptance, not completion.
---At least one of energy/impulse must be positive. No occlusion/shielding or
---ordinary non-destructible body blast. Optional FractureDebris3D emits cosmetic
---chips on accepted contacts, independently of the later structural commit.
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

---Returns a versioned geometry-free checkpoint of committed damage and fragment
---poses. Fails while commands are queued. Save this through demi.save if desired.
---@param entity_id string
---@return table|nil checkpoint
---@return string error
function Destruction3D.checkpoint(entity_id) end
---Queues checkpoint restoration on a fresh attached assembly. Geometry hash,
---part identities, numeric bounds and partition/support are checked. Poll state.
---@param entity_id string
---@param checkpoint table
---@return boolean accepted
---@return string error
function Destruction3D.restore(entity_id, checkpoint) end
---Opt-in cleanup of detached dynamic groups at the fixed-step boundary.
---Supported geometry remains; checkpoints remember retired debris as missing.
---@param entity_id string
---@return boolean accepted
---@return string error
function Destruction3D.retire_debris(entity_id) end

return Destruction3D
