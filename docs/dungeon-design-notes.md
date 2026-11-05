# Dungeon design notes

## Research translated into decisions

- [Mike Shea's dungeon-map checklist](https://slyflourish.com/simpler_jaquay_style_maps.html)
  emphasizes useful alternate routes, asymmetry and secrets. The application here
  is an optional vault whose mechanism also opens a return route, while preserving
  an independent path to the stairs. Simply adding more rectangular rooms did not
  produce a meaningful choice.
- [Scott Rehm on exploration choices](https://theangrygm.com/pretty-good-dungeon-how-players-explore/)
  argues for distinguishable options and information that gives players reasons
  to investigate. Keeper notes, unusual masonry and visible barred entrances now
  connect clues to rewards. The journal preserves those clues within the floor.
- [id Software's push-forward combat talk](https://gdcvault.com/play/1024940/Embracing-Push-Forward-Combat-in)
  describes resource acquisition as an incentive to engage. The adaptation is a
  small, finite mana reserve restored by successful melee and discovered supplies,
  rather than unlimited safe shots or passive waiting. This is a design choice,
  not a claim that every dungeon game should require melee.
- [SnowHex's asset page](https://snowhex.itch.io/dungeon-gathering) distinguishes its
  basic free set from the fuller collection. The supplied screenshots guide the
  composition: localized portrait/ribbons, a corner map, continuous floors and
  environmental landmarks. The implementation uses the available atlas and
  original vector frames; it does not copy mobile controls onto desktop.

## Current balance targets

Eight mana permits four Spark casts, two Frost casts or two Ember casts. Spell
ranges are 6, 5 and 4.8 world units. A landed melee swing restores two mana once.
Spark is a limited ranged option, Frost creates an approach window, and Ember
rewards grouping targets. Successful staff hits provoke pursuit and nearby allies;
home distance does not erase an active alert. No passive regeneration sustains
camping. These values need play feedback, not just correctness tests.

## Exploration and presentation acceptance

- A player can reach stairs with all enemies alive and all vault gates closed.
- Entered room terrain and boundary walls remain discovered. Live actor visibility
  remains occluded; minimap icons do not disclose unseen enemies or distant exits.
- A lever changes both native collision and navigation, and opening a secret gives
  access to an otherwise unreachable reward. Notes and open gates survive Continue.
- Repeated translucent fog edges must not produce dark grid seams. Visibility rays
  stop at the target tile's near face; exposed wall neighbours stay readable.
- HUD information stays in compact corners and quick slots. Full-width debug-like
  panels and the permanent New Run button are removed from normal play.
- Floor scenes and HUDs are separately inspectable. Static geometry is authored in
  tilemap assets, and shared UI remains in reusable prefabs.

Thirty domain tests and the native playthrough suite check these contracts. They
cannot prove that the game is fun. The useful next play feedback is whether players
notice a clue, choose to pursue it, use different weapons for different situations,
and feel that choosing to flee or explore was consequential.

## Authored exploration and readable progression (2026-11-01)

Primary RPG references consulted:

- [Crate Entertainment: Grim Dawn exploration](https://www.grimdawn.com/guide/gameplay/exploration/)
  describes handcrafted routes, secrets, optional dangerous areas and shortcuts.
- [Crate Entertainment: Grim Dawn masteries](https://www.grimdawn.com/guide/character/masteries/)
  describes choosing complementary character specializations rather than only
  accumulating automatic stat increases.

Application here is deliberately smaller: the first three authored maps expand
from 35x26 to 60x46, retaining old areas and stable state identities. Added wings
have quiet rooms, a western reward branch, a statue encounter, an eastern timed
trap route with a disarming lever, and a refuge. Both loops reach the stairs
without an enemy-clear requirement. The entrance duelist moves to an optional
reliquary, giving the starting merchant and note room breathing space.

The statue sequence pauses combat, moves two guards around their plinths, and
returns control with an attack grace period. E skips to the same end state.
It is gameplay choreography in Lua using native transforms and camera follow,
not a prerecorded movie or an extension to the engine's cutscene status flags.

Exploration grants XP once per discovered room, chests grant XP once, and combat
continues to award XP. C opens Vigor, Blade and Arcana talent choices. Rank two
adds armor, dodge recovery or reduced spell cost respectively. An XP meter and
unspent-point indication make this progression visible. Version 3 checkpoints
retain old actors/items/discovery and acquire appended wing identities; they are
backed up before a version 4 save replaces them.

The blue banner-shaped objects were waterfall frames stretched across a solid
pool collider. A bounded pool texture now fills that same authored collision
rectangle. Backpack and journal buttons use colored SVG textures with small key
hints. Native smooth fog replaces tile-sized fog sprites; sight extends from
7 to 11 units, with a feathered live edge and interpolated discovery memory.

These are design decisions and testable mechanisms, not proof of entertainment
value. Room variety, combat balance and repeated-run appeal still need human
playtesting; the example is not a full commercial RPG campaign.
