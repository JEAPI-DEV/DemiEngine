### Scenario A — Visual novel

Developer workflow:

1. Run `demi new my_story --template visual-novel`.
2. Replace template characters, backgrounds, music, and fonts through normal
   asset manifests.
3. Add chapters as schema-validated `DataAsset` documents with stable node and
   line IDs.
4. Add localized strings independently of layout and story topology.
5. Configure the reusable dialogue package and theme rather than editing its
   traversal code.
6. Preview long/short/CJK/RTL pseudo-locales at desktop and phone viewports.
7. Test every choice path, invalid reference, save point, migration, auto/skip,
   voice-missing fallback, and touch/keyboard/controller input headlessly.
8. Profile backlog/text cache and transition memory, then package Linux and
   Android from the same story data.

Acceptance project:

- at least two chapters and two backgrounds;
- three layered characters with expression changes;
- a conditional choice depending on earlier state;
- music crossfade, voiced and unvoiced lines, and one cutscene;
- English plus a pseudo-localized expansion locale;
- save on ordinary line and choice, quick-save/load, auto-save on chapter, and
  migration after one node rename;
- backlog virtualization, auto, skip-seen, hide UI, history, and settings;
- automated traversal proves every reachable node terminates or intentionally
  loops and every referenced asset/localization key exists.

No-engine-edit gate: adding a chapter, character, expression, choice operator
already supported by the package, language, or save slot changes no C++ and no
shared package internals.

### Scenario B — Platformer or top-down action game

Developer workflow:

1. Create the corresponding template and choose/install only the movement,
   health/damage, projectile, checkpoint, interaction, and camera packages the
   game needs.
2. Author player/enemy/pickup/projectile prefabs and tuning `DataAsset` files.
3. Build tilemaps/object layers and declare collision/navigation metadata.
4. Connect package events to game-specific score, animation, audio, and level
   progression policy.
5. Record deterministic input replays for movement, damage, death, checkpoint,
   scene transition, and save/load.
6. Stress projectile speed, pooling, simultaneous contacts, runtime tile edits,
   pause/resume, and aspect/touch variants.
7. Set budgets for physics, Lua, particles, batches, assets, and UI.

Acceptance project:

- keyboard/gamepad/touch actions use the same controller script;
- two enemy behaviors share damage/health without inheriting a common engine
  class;
- hitscan and pooled physical projectile modes both handle triggers/solids;
- checkpoints cross scene boundaries and restore a versioned save;
- dynamic tile or blocker updates render, physics, and navigation together;
- destruction during callbacks and release/reclaim in one frame are tested.

No-engine-edit gate: a new weapon, enemy, pickup, room, or tuning profile uses
prefab/data/package extension points only.

### Scenario C — Isometric builder or tactics game

Developer workflow:

1. Start from the isometric template with grid, occupancy, projection,
   navigation, selection, and placement already connected.
2. Define buildings/units/terrain/economy as data and prefabs.
3. Compose optional health, interaction, objective, wave, and save packages.
4. Keep build rules, turn/economy policy, targeting, AI, and victory conditions
   in game modules.
5. Validate footprint, path preservation, unreachable goals, stable sorting,
   and save references before play.
6. Replay large waves/turns with dynamic blockers and profile pathfinding,
   entity count, draw batches, and UI updates.

Acceptance project:

- rectangular and multi-cell footprints;
- placement preview with deterministic rejection reason;
- multiple agents/factions and at least two movement costs;
- path invalidation while agents are moving;
- selection and command input on mouse, controller focus, and touch;
- save/load during a wave/turn with stable prefab/entity IDs;
- hundreds of units/buildings remain within declared budgets.

No-engine-edit gate: a new building, unit, map, objective, targeting strategy,
or economy rule remains project data/Lua.

### Scenario D — Lightweight 3D exploration or action game

Developer workflow:

1. Create the lightweight-3D template and import a third-party glTF with an
   explicit profile.
2. Inspect normalized nodes/materials/animations/bounds and accept generated
   collider recommendations.
3. Compose character/camera/environment/pickup/projectile prefabs from public
   components and packages.
4. Use diagnostic views to correct source import/material data rather than
   adding model-specific renderer branches.
5. Replay grounding, slopes, steps, moving platforms, triggers, projectile
   casts, pause, and scene transitions.
6. Test culling, instancing, lights/shadows, textures/alpha, animation, and
   resource reload on Noop/Linux/Android paths as applicable.
7. Enforce mobile and desktop budgets before expanding the scene.

Acceptance project:

- mixed primitive meshes plus one skinned animated character;
- explicit non-default source axis/scale conversion;
- static triangle mesh, dynamic convex, trigger, and capsule controller;
- moving/rotating platform, slope/step, pickup, projectile, and door;
- multiple material texture modes including alpha cutoff;
- directional/point/spot light within declared limits;
- load/unload/reload cycles do not retain GPU/Jolt/animation resources.

No-engine-edit gate: replacing the character or environment model, adding a
clip/material/light/pickup, or changing collider detail is asset/prefab data.

### Scenario E — Multiplayer action game

Developer workflow:

1. Build and test the complete offline/local game path first.
2. Declare replicated prefabs, permitted fields, RPC schemas, rates, and
   authority rules.
3. Compose the lobby/session package and explicit scene-readiness policy.
4. Run a local headless server plus clients with deterministic normal, latency,
   loss, reorder, duplication, and disconnect profiles.
5. Test late join, ownership transfer, reconnect, scene transition, and server
   shutdown before adding prediction.
6. Add prediction/reconciliation only to measured latency-sensitive state and
   compare authoritative replay results.
7. Enforce payload, rate, bandwidth, server-tick, entity, and buffer budgets.
8. Package headless Linux server and Android/Linux clients from one project.

Acceptance project:

- lobby, ready state, match scene, results, and rematch/reset;
- at least two replicated prefab types and two authority models;
- reliable discrete events plus unreliable state/input flow;
- late join receives all current entities/session state automatically;
- disconnect removes/transfers exactly the correct state;
- invalid/unauthorized/oversized messages are rejected and counted;
- deterministic network simulations reproduce their result from a saved seed.

No-engine-edit gate: adding a replicated prefab, RPC, lobby field, map, or game
mode uses metadata, schema, prefabs, and Lua policy.

### Scenario F — UI-heavy management or RPG game

Developer workflow:

1. Model items, characters, quests, recipes, and balance values as validated
   data assets.
2. Build reusable UI prefabs and virtualized lists/grids projected from that
   data.
3. Keep domain state in explicit Lua models and saves; UI nodes remain views
   with typed commands/events.
4. Test keyboard/controller/touch focus, search/filter, drag/drop, modal state,
   locale expansion, safe areas, and save migrations.
5. Profile layout/text/list virtualization with production-scale fixture data.

Acceptance project:

- thousands of item definitions and a virtualized inventory;
- equipment, quest, settings, confirmation modal, and searchable list screens;
- focus restoration after filtering/removal and correct multi-pointer capture;
- versioned saves reference stable content IDs and diagnose removed content;
- no per-frame rebuilding of unchanged UI or content tables.

No-engine-edit gate: adding content fields within declared schemas, screens,
filters, commands, or localization remains data/Lua/UI work.

## Delivery Order

Implement the steps in this order unless a reference game demonstrates a
blocking dependency:

```text
1 Project workflow
  -> 2 Game data
    -> 3 Text/UI and visual novels
      -> 4 Reusable 2D/isometric kits
      -> 5 Lightweight 3D workflow
      -> 6 Multiplayer workflow
        -> 7 Assets, streaming, and packages
          -> 8 Editor
            -> 9 Shipping
              -> 10 Performance and reliability
                -> 11 Advanced real-time networking (when required)
                -> 12 Dockable editor workspace (deferred)
```

Steps 4, 5, and 6 may proceed independently after Step 3. Performance tests
and failure coverage from Step 10 are added throughout the roadmap rather than
postponed until the end. Step 11 depends on the secure ownership and replication
contract from Step 6 and is not required for turn-based, cooperative, or
latency-tolerant multiplayer games. Step 12 depends on the editor contract from
Step 8 and the lifecycle/reliability gates from Step 10; it does not depend on
advanced networking and may be scheduled independently after those gates.