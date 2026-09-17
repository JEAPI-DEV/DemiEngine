---@meta
-- Native module: require("demi.destruction3d"). Annotations only.
---@class Destruction3DState
---@field root string
---@field status string unattached, ready, queued, applied, or failed
---@field error string
---@field revision integer
---@field bodies integer
---@field parts table<string,string> Collider part ID to its current physical entity ID.
---@class Destruction3DService
---Destructible3D assemblies and Fracture3D mesh components generate mappings and ModelCollider3D.inline_geometry;
---gameplay should not maintain these generated collider payloads manually.
local Destruction3D = {}
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
