# Spatial destruction impacts (first implementation)

Import `demi.physics.destruction3d` and supply world-space impact data. Gameplay
does not select shard names or manually push newly split bodies:

```lua
local Destruction = require("demi.physics.destruction3d")

local accepted, issue, affected = Destruction.impact({
  entity = hit.entity_id,
  position = hit.point,
  radius = 0.3,
  energy = 6000,
  impulse = 180,
  direction = ray.direction,
})
```

Omit `entity` to affect nearby attached destructible assemblies. A supplied root
or current fragment ID limits the query to its owning assembly, not just that
fragment. Omit `direction` (or supply zero) for radial impulses. Nonzero directions
are normalized; exactly coincident radial contacts use +Y. Position and direction
are three-number arrays. Radius defaults to 0.5 metres; energy and impulse default
to zero, and at least one must be positive. Unknown fields and malformed values
are rejected. Attachment requires the first physics step.

## Selection, resistance and budgets

The native Jolt sphere query retains compound part identity and resolves contacts
against the real collision shapes, including moved, rotated and scaled bodies.
Distance is zero inside a part, otherwise distance to its collider surface.
Linear falloff is `w = max(0, 1 - distance / radius)`.

Only live bonds incident to contacted parts are candidates; each bond uses the
larger of its endpoint weights. Live foundation attachments use their part's
weight. Broken bonds in a still-connected graph are excluded using Blast's live
health. Selection remains limited by the authored fragment resolution: touching
one large rigid fragment can stress connections elsewhere on that fragment.

The total fracture energy, in joules, is divided between candidates as
`energy * w / max(1, sum(weights))`, across all affected assemblies. This keeps
single-target falloff and does not multiply energy per fragment or structure.
`Destructible3D.energy_per_health` converts each share into existing bond-health
units; it defaults to 1000 J per unit. Authored bond/anchor strengths still govern
failure. This is a scalar resistance model, not material stress, strain or fracture
toughness derived from mesh area. Legacy `damage_part` still takes health units.

Impulse is a separate momentum budget in N*s, distributed over contacted parts
with the same bounded normalization. It is not automatically inferred from the
fracture energy and does not guarantee debris kinetic energy equals that energy.
Gameplay can supply measured collision energy, but automatic collision-driven
destruction and secondary impacts are not wired up by this API.

## Queuing and physics

Acceptance means queued, not completed. Inspect `Destruction.state(root)` for
`queued`, `applied`, `failed`, error and revision. Revision advances for successful
batches, including impulse-only hits on already detached pieces.

All affected queues are prepared before enqueue publication. Assemblies then
commit independently through the existing fixed-step transaction. Impulses are
applied only after that assembly's proposal succeeds, to each part's resulting
physical owner. Application points follow the part's local coordinates while
queued. Native off-center impulses produce rotation as well as translation.
Anchored groups absorb their impulse share without moving. A failed split applies
neither its proposed damage nor its queued impulses; root removal cancels its work.

Limits: radius 0.001–1000, energy 0–1e12, impulse 0–1e9; position/direction axes
must be finite and within +/-1e6. Query results, affected assemblies, and queued
part impulses no longer have the former 8192/32/512 count caps. Proposal storage
grows with the affected content. Failed proposal preparation leaves earlier
accepted work intact. Large bursts can still consume substantial CPU time and
memory; removing these caps is not a frame-time or memory guarantee.

`Destruction3D.impact` profiles selection/allocation and `Destruction3D.update`
profiles the fixed-step processing. The current scheduler processes one queued
family per step in stable order; fair/resumable scheduling, burst performance and
Android qualification remain open.

## Demo and remaining work

`demi run --project examples/fracture_prefab_lab`: LMB issues a small directional
strike; RMB issues a larger radial blast; R resets. Both walls use one ordinary
component-authored prefab. The Lua probe only supplies impact parameters and
displays status—no part-name selection or manual post-split impulses.

Tests cover edge hits, radius misses, transform handling, cumulative resistance,
falloff, multi-assembly energy sharing, target filtering, isolated foundation
release, impulse-only hits, off-center torque, queue growth and rollback.
Regression checks cover 40 affected assemblies and 513 queued impulses.
Repeated full hammer-energy hits also exercise detached blocks after they settle
and sleep, including heavy bodies. Exhausted bonds do not prevent later impulses;
applied impulse does not guarantee large displacement against friction, remaining
supports or surrounding debris. Single shards cannot recursively fracture yet.

For the playable hammer/rocket/steel-door probe, use
[`destruction_weapons_3d_lab`](../examples/destruction_weapons_3d_lab/README.md).
It uses a reusable gameplay package, timed contact and swept projectile flight.
There is no occlusion/shielding, blast response for ordinary non-destructible
bodies, recursive shard refinement, structural stress or debris cleanup policy.
