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
