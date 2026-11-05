# Dungeon RPG and UI skin qualification

The maintained public UI contract is documented in the website `hud-nodes`
template. Gameplay and controls are in `examples/Dungeon/README.md`.

## Combat correction

The original example tested projectiles against a small circle at the actor's
ground position. That circle did not cover the rendered character. The example
now shares sprite dimensions, pivot and projectile origins through
`dungeon/body.lua`; swept slab tests cover the visible body and return the first
intersection along a shot. Ground movement continues using the native kinematic
collider and wall queries. Combat volumes do not enlarge movement colliders.

The body tests exercise feet, torso, head, outside-body misses and the larger
sentinel/boss scales. The scripted traversal checks the projectile origin and
aim point against walls rather than assuming ground-level visibility makes
every point on a tall sprite shootable.

## Shared UI skin support

HUD images accept `nine_slice` source-pixel borders and a positive
`nine_slice_scale` canvas multiplier. `UiImageSkin` owns their value defaults and
parsing/validation. Expanded HUD/prefab validation, runtime parsing, native HUD
rendering, Inspector controls, JSON schema, Lua creation metadata and CLI JSON
inspection share that contract. Ordinary unsliced images retain aspect-fit
behavior.

`Canvas2D::ninePatch` now skips cells that collapse when borders are compressed
into a small destination. This also supports zero-width authored borders; it
does not weaken the rejection of an invalid overall destination. Tests cover
normal nine-patch geometry, compressed corners and zero-sized HUD images.

The Dungeon example uses the supplied SnowHex atlas directly for frames,
buttons, inventory slots and equipment icons, plus its heart sheet. Local UI
prefabs compose skinned images and normal button children. There is no native
Dungeon-specific widget or second HUD model. The original pack licenses remain
attached to the imported resources.

## Gameplay and persistence

Pure Lua owners handle items/stacks/equipment, trading, loot/experience and
versioned checkpoint data. Runtime adapters own native UI, scene entities and
the Save service. Equipment changes actual combat/movement values. Pickups and
chests remain available when the bag is full; transactions are atomic, and
equipped items cannot be sold or dropped before unequipping.

Checkpoints serialize sparse inventory slots as indexed records, retain stable
item/entity IDs and validate the result before rebuilding a floor. Enemy route
caches are recomputed rather than persisted. Invalid/incompatible checkpoints
are reported and retained. Production saves use platform player data; E2E uses
the separate `hollow_crown_e2e` slot.

## Qualification

Linux Release runtime and editor were rebuilt. Focused coverage:

- Twenty-two Lua domain tests, including inventory capacity, equipment, damage
  reduction, consumable expiry, one-time rewards and checkpoint restoration.
- Native Canvas2D, HUD image rendering and UI-prefab validation tests.
- Editor HUD field edits, failed-edit rollback, Undo/Redo and save/reopen.
- CLI HUD inspection, example validation and headless startup checks.
- Visible Vulkan E2E: wall collision, dodge, pause/resume, defeat/restart;
  actual inventory/chest/equipment/trader controls and checkpoint Continue;
  ordinary gameplay actions clearing all four floors and the boss.
- Linux cooking and cooked-runtime startup.

Inventory and trader screenshots are generated under the ignored example
`build/qualification/` directory. This is focused Linux qualification, not a
full repository test gate, Android/device qualification or a performance claim.

## Melee and encounter redesign

Sword item IDs now describe real short/long swords; the staff remains ranged.
`melee.lua` is the single owner of sweep ranges, arcs and phase durations used by
combat and blade/telegraph presentation. Hits are swept across the active angular
interval, respect native wall queries and occur once per victim. Tests include
windup immunity, differing reach, locked aim, behind-attacker misses, wall
occlusion, large timesteps and dodge cancellation.

Patrolling/flanking duelists, committed long-sword guards, ambush crawlers and
retreating casters have distinct decisions. Named rooms, cover, alternate
crossings, spike lanes, optional shrines and guarded ward sigils replace the
kill-every-enemy floor gate. All objectives and treasure positions have connected
path coverage. The native suite adds a real short-sword duel without projectiles.

Checkpoint version 2 retains ongoing attacks and encounter state. Version 1
migrates inventory IDs and retains player progression, but restarts the current
floor because its layout changed. The previous save is backed up before a
version-2 write; unsupported saves are not deleted.

## Paused inventory weapon change

Equipping or unequipping a weapon cancels an unfinished melee attack while
retaining its cooldown. Waiting for the attack to finish was a deadlock because
the inventory pauses simulation. Phase-specific rule tests cover windup, active
swing and recovery; a native UI regression opens the bag during a real attack,
changes weapons twice, closes it and verifies recovery resumes without old hits.

## Discovery, resources and authored floors

The current version removes all enemy/sigil requirements from stairs. Optional
vaults use linked levers or hinted secret walls. Room discovery and live actor
visibility are separate, and fog cells no longer overlap. Visibility uses target
tile entry distance and remembers room boundaries instead of hiding oblique walls.

Magic now has finite mana, bounded range, three distinct profiles and melee/resource
refills. Hits explicitly provoke enemies and nearby allies. The native query test
exposed disabled entities remaining in raycasts and overlaps; PhysicsQueries2D now
filters them consistently, with disable/re-enable and trigger regression coverage.
Water uses a movement-only pit layer rather than an opaque wall layer.

The four floor/boss scenes each reference authored Tilemap2D assets and their own
HUD composition. Map geometry and encounter metadata share one JSON source exposed
as typed Tilemap2D/DataAsset resources. Common gameplay, inventory, dialogs and boss
UI are reusable prefabs. A compact portrait/ribbon HUD replaces the old dashboard.

Current qualification is 30 Lua rule tests and seven native E2E scenarios, including
resource exhaustion, vault collision removal, journal/save restoration, real scene
transitions without clearing enemies, and a complete mixed-combat expedition.
See [design notes](dungeon-design-notes.md) for research and remaining playtest questions.

## Wall-aligned visibility boundaries (2026-11-02)

The marked throne-room artifacts came from using the same inward feather for
both maximum sight range and every collider hit. This darkened otherwise visible
floor before walls. Taking the nearer of two angular samples also produced
scalloped planar edges; a second spatial blur widened the artifacts.

The mask now clips continuous surfaces against the segment between adjacent ray
hits. Depth discontinuities retain the nearer blocker rather than connecting a
pillar to a distant wall. Range feathering applies only to the sight radius.
Collider-distance padding and the extra blur pass are removed; linear texture
sampling provides narrow edge antialiasing. Discovery memory is unchanged.

Focused coverage checks bright floor immediately before a flat wall, darkness
behind it, the projected pillar-corner shadow, and a zero-distance blocker.
A Linux Release bgfx/Vulkan visual probe recreates the throne room at player
position (22.5, 6). The inspected capture shows floor illumination reaching the
bottom/right collider boundaries without the former broad dark bands. The
Dungeon project validates with no diagnostics. This is a rendering correction,
not a change to attack range, AI detection, collision, or exploration rewards.

After this correction, the focused fog CTest, all 34 Lua rule tests and all eight
Linux desktop E2E scenarios pass. The final rendered comparison is available in
`examples/Dungeon/build/review/wall-boundary-after.png` (ignored review output).

## Corner sampling and edge coverage (2026-11-02)

The wall-band correction still left binary mask texels and uniformly spaced
corner samples visible on magnified diagonals. The follow-up uses paired collider
rays bracketing surface transitions and fractional distance-to-edge coverage in
the mask. Range falloff stays separate; there is no full-image blur pass.

References checked against the implementation:
[Nicky Case, Sight & Light](https://ncase.me/sight-and-light/) explains the failure
of evenly spaced rays and casting on both sides of segment endpoints;
[Red Blob Games, 2D Visibility](https://www.redblobgames.com/articles/visibility/)
explains endpoint-based polygons and combining visibility with range falloff.
DemiEngine adapts the corner principle through existing native collider queries,
using bounded refinement instead of duplicating collider geometry in Lua.

The follow-up passes both focused CTests: mask coverage and native corner-query
refinement (including moving observer positions and disabled blockers). Project
validation reports no diagnostics. A fresh Linux Release bgfx/Vulkan capture at
2560x1440 is stored in `examples/Dungeon/build/review/shadow-edges-aa-2560.png`.

## GPU fog ownership and movement profiling (2026-11-03)

The CPU bitmap path has been replaced by compact polygon/discovery state and a
native GPU shader. See the [architecture audit](engine-architecture-audit-2026-11-03.md)
for the matched editor comparison and remaining tail latency. Native collider
queries share exact intersection code through a call-scoped radial broad phase.

`scripts/test_fog_rendering.py` tests real Vulkan-rendered pixels for pillar
occlusion, bright floor before walls, fractional edge coverage, range fade,
discovery memory, and an opened blocker after its queued command commits. The
shader compiles for Vulkan, OpenGL and OpenGL ES; Android device qualification
was not performed. CPU-only mask pixel tests were replaced by compact-state
validation and these rendered assertions.

## UI migration rollback (2026-11-04)

The RmlUi experiment was reverted at the user's request. The existing JSON HUD
kit, visual editor, event annotations and pre-migration Dungeon presentation
were restored. The later gameplay, progression, maps, collision and GPU fog work
remain intact. Native-RML qualification results from the experiment do not
qualify the current implementation.

## Journal paragraphs and fog/dev-mode regressions (2026-11-05)

Keeper notes now contain authored paragraph breaks. Journal display uses current
note spacing for older saved text whose words match, without rewriting the save.
The repeated journal footer is hidden; reading a note emits a short notification.
The Dungeon E2E covers the older-note presentation and unchanged stored text.

A native corridor regression reproduced a visibility leak near (29,14.5): a
polygon ray reached 1.85352 units where the actual wall began at 1.5. Uniform ray
bins could miss narrow blockers or multiple surface transitions. The fan now
seeds native collider corners/tangents and refines both sides of transitions;
curved surfaces have an explicit midpoint error bound. Range sampling preserves
the previous maximum-radius chord error while using 107 rather than 512 uniform
rays at Dungeon's radius 11. The fog shader's antialiasing is one drawable pixel,
rather than a fixed fraction of a world cell that expands when zoomed in.
Dense CPU comparisons cover seams, angular wrap, narrow boxes/circles and curved
surfaces. The four-phase maximized Vulkan pixel probe passes at 3840x2022; evidence
is in `build/linux-release/qualification/fog-corridor-20261105/`.

The UI rollback had restored eager layout calls in Lua mutation wrappers despite
retaining document invalidation. Those calls are removed; input, accessibility
and drawing resolve the final geometry. A 100-position Lua-host regression verifies
one layout after the batch. Recycled row placement/visibility also invalidates
at its owning mutation boundary. Maximum layout passes in the exploration trace
fell from 40 to 1 per frame.

`demi dev` also scanned the entire project tree every frame, before profiler
frame collection. It now uses the already-linked libuv filesystem event backend,
with one watch per admitted directory. Scans reconcile actual changes; idle polls
do not walk files. Unsupported filesystems/watch-limit failures use a 500 ms polling
fallback. Tests cover nested creation/editing, atomic replacement, directory moves,
root deletion/recreation, reset and ignored generated outputs. HotReload.poll is
recorded after profiler beginFrame so its cost remains visible.

Matched Release standalone dev runs used 3840x2022, the same engine-driven 60-second
room traversal, and isolated saves. Both ended at (5,10.020), waypoint 9, with 1105
cells discovered. The probe grants extra health to keep traversal alive; navigation
probe overhead is recorded separately. An existing user dev session remained
open in the background. The original 60 FPS request was retained.

| Moving phase | Before | Final |
| --- | ---: | ---: |
| CPU including dev watch, median |8.055 ms|6.210 ms|
| CPU including dev watch, p99 |12.106 ms|10.116 ms|
| CPU including dev watch, maximum |21.593 ms|11.303 ms|
| Dev watch, median |1.844 ms|0.005 ms|
| Fog CPU, median |1.483 ms|1.100 ms|
| GPU median, independent delayed clock |1.003 ms|1.015 ms|
| Frame interval, p99 |16.769 ms|16.774 ms|
| Frame interval, maximum |21.996 ms|17.503 ms|

These are matched measurements, not a guarantee against all hitches or a 165 Hz
qualification. Actual source reloads still perform work. The native runtime/editor
must be restarted to load C++/embedded shader changes. Six focused CTests and all
eight Dungeon E2E cases pass. Performance CSV/JSON/logs are under
`build/linux-release/qualification/dungeon-fog-dev-performance-20261105/`.

Reproduce the measured route with:

```sh
python3 scripts/profile_dungeon_movement.py --dev --explore \
  --output build/linux-release/qualification/dungeon-dev-route
```

The default is maximized windowed; the driver uses engine actions and isolated
user data. `--window-size` is reserved for explicit fixed-resolution comparisons.
The final route's largest polygon had 642 points, within the existing 4096-point
contract. The ordinary 14-second stationary/movement probe remains available by
omitting `--explore`.

### Observer-origin fog wedge regression (2026-11-05)

The reported actor-like shadow reproduced in the GPU probe with all wall
colliders disabled and no actors. Its origin was the fog focus, not a character
collider. Floating-point modulo in the boundary texture lookup failed to wrap
some exact ring boundaries correctly, sampling an unused texel. Replace modulo
with explicit single-wrap comparisons for the bounded neighbour indices.

The old maximized Release capture failed 27 of 1440 sampled directions at radius
5 in the empty view. The expanded GPU test checks all 1440 directions at four
radii, both at the origin and after moving the focus and changing sight radius.
Player/enemy colliders on the actor layer, including a rotated/moved enemy, stay
excluded. All five GPU phases and both native visibility/fog CTests pass.
Evidence: `build/linux-release/qualification/fog-actor-investigation/` (before)
and `build/linux-release/qualification/fog-seam-fixed/` (after), at 3840x2022.
The runtime and editor Release binaries were rebuilt; restart a running native
session to load the embedded shader. No gameplay or authoring contract changed.
