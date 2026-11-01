# The Hollow Crown

A desktop dungeon RPG for DemiEngine with melee combat, limited magic, exploration,
optional treasure vaults, equipment and checkpoint saves. **The stairs never require
clearing enemies or collecting sigils.** Reach them and press E to descend. The
fourth floor contains the final boss.

```sh
cmake --build --preset linux-release --target demi demi-editor
./build/linux-release/demi run --project examples/Dungeon --window-size 1200x800
```

Use a fresh launch after updating Lua, maps or HUD sources. A running game caches
modules and assets. New Expedition starts from floor one; Continue restores a save.

| Control | Action |
| --- | --- |
| WASD / arrows | Move |
| Mouse + LMB / Space | Aim and attack |
| X | Cycle carried weapons between attacks |
| Left Shift | Dodge; cancels a committed sword swing |
| E | Interact, inspect a wall, pull a lever, or descend |
| I / bag icon | Inventory and equipment |
| J / notes icon | Discovered notes and secrets |
| Q / red-potion slot | Drink a healing potion |
| T / blue-potion slot | Drink a mana flask |
| Escape / pause icon | Pause; save, return to title or start a new expedition |
| F5 | Save |
| R | Restart the live expedition without deleting the checkpoint |

The HUD follows the supplied reference composition: a portrait with tapered health
and mana ribbons at the upper left, coins below it, a compact floor map at the upper
right and desktop quick slots along the bottom. It has no mobile joystick or large
permanent header/footer. Notifications expire; interaction prompts are contextual.

## Combat and resources

| Weapon | Role and cost |
| --- | --- |
| Short sword | Fast 1.55-unit slash; 0.13s windup, 0.12s active, 0.23s recovery |
| Long sword | Broad 2.65-unit sweep; 0.55s windup, 0.25s active, 0.65s recovery |
| Spark staff | 2 mana, 6-unit range, 0.8s cooldown; four casts from a full reserve |
| Frost staff | 3 mana, 5-unit range; slows a surviving victim for 2.5s |
| Ember staff | 4 mana, 4.8-unit range; bursts against a group on impact |

You have eight mana and **no passive regeneration**. A successful melee swing
restores two mana once, even if it hits several enemies. Breakable urns, caster
remains, limited flasks and one-use shrines provide additional supplies. Misses
still cost mana. Spell range expires during flight; spells cannot cross the map.

Damage alerts the victim and nearby allies in the same encounter. Provoked enemies
remember the attacker and do not immediately reset at their old home-distance
limit. Fast enemies telegraph a pursuit burst. Duelists flank, sword guards commit
to broad cleaves, crawlers pounce, and casters retreat and reposition.

Sword direction locks during windup. Yellow markers show the committed arc; red
marks the active sweep. Each target takes at most one hit per swing. Walls block
swords and projectiles. Ground colliders remain smaller than visible-body damage
volumes. Equipping from the paused bag cancels an unfinished swing but retains its
remaining cooldown, so equipment changes cannot deadlock or grant free attacks.

## Exploration

Entered rooms and their boundary walls remain on the explored map. Live visibility
still uses occlusion for actors. Unvisited areas, secret chambers and enemies behind
cover are not revealed on the minimap. Fog tiles do not overlap, avoiding dark seams.

Keeper notes distinguish routes and record clues in the journal. Inspect cracked
masonry to find secret chambers. Levers open optional vaults and their return routes;
the stairs remain reachable with every vault closed. Chest rewards sit behind these
encounters and mechanisms rather than being sprinkled in every room. Frost and
Ember staffs are discovery rewards. The starter sword and staff remain available
without exploring a vault.

Water blocks movement but not sight or projectiles. Spike lanes warn before
activation, damage enemies as well as the player, and hit at most once per cycle.
Sigils offer optional souls and mana; they never gate floor progression. Shrines
restore health and mana once. You can rush onward under-equipped or explore for
resources and better equipment.

## Authored scenes, maps and HUDs

Each floor is a real non-additive engine scene:

```text
scenes/
  floor1.scene.json     floor1.hud.json
  floor2.scene.json     floor2.hud.json
  floor3.scene.json     floor3.hud.json
  boss1.scene.json      boss1.hud.json
ui/screens/
  gameplay.ui.prefab.json
  inventory.ui.prefab.json
  dialogs.ui.prefab.json
  boss_bar.ui.prefab.json
ui/kit/
  frame.ui.prefab.json
  button.ui.prefab.json
assets/dungeon/maps/
  floor1/floor1.tilemap.json
  floor2/floor2.tilemap.json
  floor3/floor3.tilemap.json
  boss1/boss1.tilemap.json
```

Open a scene or HUD source in the editor to inspect that floor. The static map uses
native `Tilemap2D` rendering and collision. Each map JSON is one authoritative source:
its layers describe geometry, while its `dungeon` block describes encounters, rooms,
clues and rewards. A Tilemap2D manifest and a DataAsset manifest provide typed views
of that same file. Dimensions and walkability are derived from the map layers.
Coordinates use one world unit per tile, with the map at `[-0.5, -0.5]` and 16 pixels
per unit.

Floor HUD files compose common UI prefabs; only the boss scene adds the boss bar.
The game UI adapter owns the prefab namespaces, so gameplay code does not scatter
long widget paths. Native scene transitions carry the run to a new controller
through `dungeon.session`; inventory and progression are not reset. Checkpoint
publication happens after the new scene starts successfully.

## Gameplay owners

| Source | Responsibility |
| --- | --- |
| `scripts/main.lua` | Lifecycle, desktop input and real scene transitions |
| `dungeon/run.lua`, `floors.lua` | Expedition progression and authored map data |
| `dungeon/combat.lua`, `body.lua`, `melee.lua` | Damage, visible-body hits and timed sweeps |
| `dungeon/magic.lua`, `threat.lua`, `enemies.lua` | Spell economy and enemy response |
| `dungeon/exploration.lua` | Discovery, levers, clues and secret gates |
| `dungeon/items.lua`, `inventory.lua`, `loot.lua` | Equipment, transactions and resources |
| `dungeon/world.lua` | Native entities, navigation and queries |
| `dungeon/*_view.lua`, `presentation.lua`, `menus.lua`, `ui.lua` | Presentation and UI interaction |
| `dungeon/checkpoint.lua`, `persistence.lua` | Validated state and native save storage |

The source paths in the table are relative to `scripts/`, except `scripts/main.lua`.
Movement, pathfinding, collision and rendering remain engine services. The game owns
combat rules and exploration policy rather than duplicating the engine's physics.

## Inventory and saves

The twelve-slot bag supports stacked consumables, weapons, armor and charms. Select
an item to equip, unequip, use, drop or sell it. Equipped items must be unequipped
before removal. Full bags leave rewards in the world, and shops charge only after
an item is delivered. Gear changes actual damage, defense, movement or cooldowns.

Saves retain inventory, mana, encounter state, opened gates, discovery and journal
entries. They use platform player data, never project or cooked source. Version-1
and version-2 checkpoints are backed up before a version-3 write. Migration preserves
gear, experience and currency but restarts the redesigned current floor at its
entrance. Unsupported or corrupt checkpoints are reported and preserved.

## Validation and later training

```sh
lua examples/Dungeon/tests/rules_test.lua
./build/linux-release/demi validate examples/Dungeon --platform linux
./build/linux-release/demi test linux --project examples/Dungeon --timeout 360 --window-size 1200x800
./build/linux-release/demi cook --project examples/Dungeon --platform linux --output examples/Dungeon/build/cooked
./build/linux-release/demi run --project examples/Dungeon/build/cooked
```

The standalone rules test uses Python's standard JSON parser to read the same map
sources as the engine; there are no duplicate test maps. Native tests cover real UI
controls, gate collision, saving/Continue, mana exhaustion, scene transitions with
living enemies, and a completed expedition. Test saves use separate slots.

A future agent can use `dungeon.session.set_driver(callback)`. The callback receives
a detached observation and returns movement, aim, attack, dodge, interaction, healing,
mana-flask or equipment actions. Observations include mana, visibility/discovery,
gates, inventory and combat state. This remains a game foundation, not an implemented
neural-network backend or batched RL environment.

Research, decisions and limitations are recorded in the [design notes](../../docs/dungeon-design-notes.md).
Linux Release is qualified; these checks do not establish Android or mobile support.

## Art

Dungeon scenery, torches, potions, coins and the UI atlas come from the supplied
Dungeon Gathering packs by SnowHex / Jose Javier. Their archives, licenses and
attribution remain intact. Character/weapon sprites and the compact ribbon/portrait
frames are original SVG pixel art. UI slots and panels still use the supplied atlas.
Pixelify Sans is bundled with its OFL license. Sounds are original synthesized WAVs;
`tools/synthesize_sounds.py` reproduces their sources.
