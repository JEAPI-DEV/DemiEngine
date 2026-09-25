# Cosmetic fracture debris

## Fading actual detached geometry

Use `debris_lifetime` and `debris_fade` on a source object's `Fracture3D` or
`Masonry3D` component when the real broken-off pieces should fade:

```json
"Masonry3D": {
  "size": [1, 2, 0.115],
  "debris_lifetime": 3,
  "debris_fade": 1
}
```

The default lifetime is zero, which retains normal physical rubble. A positive
lifetime applies only after an actual unsupported chunk detaches. Its original
mesh, texture, relief and interior surfaces remain visible while fading; no
replacement blocks are spawned. Its physics body is removed before the next
simulation, and its center-of-mass velocity and angular velocity continue as
non-colliding ballistic motion under the world's gravity. It can pass through
other geometry. Expiry removes the temporary body container and its visuals.

A connected chunk containing any physical source stays physical. This protects
doors and reinforcement until they separate from disposable masonry. A mixed
fading chunk uses the longest lifetime and fade among its sources. Checkpoints
record these detached chunks as retired, preserving the holes without replaying
temporary debris on restore. Explicit damage to retired parts is rejected.

The weapon-range example compares both policies: its left doorway keeps normal
physical rubble; its right prefab variant overrides only the three masonry
regions. Both doors, hinges and steelwork remain physical. The variant references
the original prefab rather than duplicating its geometry.

## Optional impact decoration (a separate effect)

Add `FractureDebris3D` beside `Destructible3D` on the assembly root:

```json
"FractureDebris3D": {
  "count": 16,
  "lifetime": 3,
  "fade_duration": 1,
  "color": [0.66, 0.47, 0.32, 1]
}
```

The component is optional and available under **Effects** in the Inspector.
It survives fracture cooking and prefab composition. No chip assets or Lua
cleanup scripts are required. This is not enabled on the weapon-range doors:
decorative chips must not imply that an intact steel panel actually fragmented.

## Emission and appearance

An accepted spatial `Destruction.impact` emits one burst per affected emitter,
at its nearest contacted part. The contact normal biases the initial velocity;
the seed gives reproducible variation in chip size, direction and spin. Chips
are an impact decoration, not extra removed structural volume. Emission does
not wait for the later structural transaction: a hit can shed chips even if
the eventual split fails. Rejected impact calls emit nothing.

Direct `damage_part` calls have no spatial contact and do not emit chips.
There is currently no automatic burst for a support failure or falling-body
collision that does not pass through the spatial impact API.

| Setting | Default | Meaning |
|---|---|---|
| `count` | 12 | Chips per accepted impact; zero disables emission |
| `max_fragments` | 128 | Live budget per emitter; zero disables emission |
| `lifetime` | 3 | Seconds until removal; zero disables emission |
| `fade_duration` | 1 | Fade during the final seconds; clamped to lifetime |
| `size` | `[0.12, 0.05, 0.08]` | Chip dimensions in metres, with size variation |
| `speed` | 2 | Initial speed scale in metres per second |
| `spin` | 180 | Maximum angular speed per axis, degrees per second |
| `gravity` | `[0, -9.81, 0]` | Cosmetic acceleration in metres per second squared |
| `color` | `[0.65, 0.45, 0.3, 1]` | RGBA tint |
| `model` | empty | Optional shared static model; otherwise cuboid chips |
| `texture` | empty | Optional base-color texture |
| `render_layer` | empty | Camera render-mask selection, as for mesh renderers |
| `seed` | 1 | Unsigned 32-bit random seed |

Custom models use their static geometry with the effect's tint/texture. This is
not a skeletal animation or per-submesh material effect. Each emitter currently
has one appearance; concrete and metal chips are not selected automatically
from the contacted material.

## Lifetime and ownership

Chips live in a separate effect store, with **no entities, colliders, or Jolt
bodies**. They cannot block a character or appear in physics raycasts. They move
ballistically, so they may pass through the floor or surrounding geometry.
Physical rubble retains its existing mass, collision, persistence and cleanup
rules independently.

When a burst exceeds the emitter budget, it replaces the oldest cosmetic chips.
If `count` exceeds the budget, only the budget-sized burst is emitted. There is
no separate fixed upper content cap. Large configured budgets still cost memory,
CPU updates and transparent draw calls; this is not a large-burst performance
qualification.

Expiry releases the fragments; an empty emitter releases its storage. Removing,
disabling or replacing the destructible owner clears its chips on the next
physics step. Scene/world teardown clears them as well. Cosmetic chips are not
included in destruction checkpoints and do not reappear on restore. Changing
settings affects subsequent bursts; existing chips keep their emission settings.

Simulation follows fixed steps, independently of camera count. Rendering uses
back-to-front chip ordering, alpha blending and depth testing without depth
writes. Chips receive lighting but do not cast shadows. The same renderer is
used by standalone Play and embedded Game View. Intersecting transparent meshes
still have ordinary object-sorting limitations; this is not order-independent
transparency. Other transparent effects are not jointly sorted with chips yet.

`Renderer3D.cosmetic_debris` reports CPU submission time and
`Renderer3D.cosmetic_fragments` reports the snapshot count for the main pass.
