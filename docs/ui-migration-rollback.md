# UI migration rollback

Completed 2026-11-05 at the user's request. RmlUi did not demonstrate the intended
maintenance benefit while preserving visual authoring and developer ergonomics.

The existing JSON HUD runtime, visual HUD editor (including prefab operations,
hierarchy, Inspector, clipboard and Undo/Redo), Lua HUD API, event annotations,
localization and example/package UI sources are restored. The RmlUi dependency,
adapters, native authoring prototype, wrappers, stylesheets and framework example
were removed from active source/build configuration. Native-only local package
releases are marked YANKED; immutable archives are retained. Original package
sources and locks select Orbit 1.1.0 and localization 1.0.0.

This was a selective rollback, not a whole-worktree reset. Dungeon's pre-native
presentation snapshot preserves the later gameplay, maps, progression, traps and
GPU fog changes. Physics wake/raycast fixes, demand-driven HUD layout, immutable
font resources, async test waits and the corrected deformation index ranges
remain. The user-authorized removal of the obsolete boss-arena example remains.

Restoration exposed an old callback lifetime bug: starting Dungeon rebuilt HUD
nodes while the dispatcher was iterating them. The dispatcher now retains the
activated payload and callback references across Lua mutation. A regression
rebuilds 512 nodes in the first action and checks the following callback's payload.
Restored layout fixtures now author layout inputs/invalidate visibility rather
than overwrite derived rectangles, retaining the prior layout-ownership fix.

Release runtime/editor rebuilt. Qualification passed: 20 focused CTests, two
package CTests, 25 example E2E cases, nine project validations and localization
startup. A visible Orbit controls test passed in a maximized 5120x2806 window;
its screenshot was inspected. Input was driven exclusively by the engine Test
API, with no OS clicks or injected keys. Test saves were isolated.

Evidence: `build/linux-release/qualification/ui-migration-revert/`.
Pre-rollback changed/untracked source and binary diff backup:
`/tmp/demiengine-rml-revert-backup-20261104-232638/`.
The stale production-example native cook cache was moved into that backup;
subsequent cooking regenerates it using the restored pipeline.
