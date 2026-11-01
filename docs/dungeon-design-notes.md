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
