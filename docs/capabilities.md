# Capability Matrix

Status meanings:

- **Stable:** covered by automated tests and part of the supported workflow.
- **Experimental:** usable, but intentionally limited or still evolving.
- **Planned:** not yet a supported engine capability.

| Area | Status | Current scope |
|---|---|---|
| Capability/API gates | Stable | Generated component and installed-Lua manifest, compatibility baseline, and seven validated reference-game gates |
| JSON projects/scenes | Stable | Versioned loading, stable IDs, CLI validation |
| Lua lifecycle and services | Stable | Lua 5.4, sol2, lifecycle, annotations, hot reload |
| Cross-platform input | Stable | Typed/contextual actions, four local gamepads, rebinding, multi-touch gestures, virtual controls, per-pointer UI capture, and replay v2 |
| Application services | Stable | Safe areas, DPI/UI scale, orientation requests, keyboard, clipboard, focus/minimize/suspend/low-memory events, and writable paths |
| 2D rendering | Stable | Pixel/normalized sprites, nine-slice, masks, startup-loaded game shader/material assets with Linux/Android stages, runtime sorting/material references, animation, multi-tileset animated tilemaps, object layers, cameras, HUD, and matching collider debug drawing |
| 2D physics | Stable | Box2D bodies, enter/stay/exit contacts, rich queries, box/circle/capsule/polygon/edge-chain colliders, materials, CCD, kinematic move/slide, forces/sleeping, and common joints |
| 2D navigation/controllers | Stable | Costed A* grid, dynamic blockers, tilemap navigation metadata, world/cell conversion, and reusable platform, top-down, and click-to-move Lua controllers |
| HUD authoring | Stable | Versioned retained trees, parameterized project-authored UI prefabs, themes, localization, and Lua mutation APIs |
| Saves | Stable | JSON slots, versions, migration hooks |
| Audio | Stable | Backend-independent mixer buses, snapshots, fades/cross-fades, scheduling, streaming, 2D/3D spatial voices, concurrency limits, entity sources, and suspend/resume |
| CLI validation and smoke tests | Stable | Project/example validation and headless runtime probes |
| Lightweight 3D | Experimental | Cycle-safe transform hierarchies, CLI-generated glTF colliders, spatial queries, versioned materials/shaders, directional/point/spot lighting, bounded shadow passes, multi-camera targets/viewports/masks, post effects, deterministic particles, world text/UI targets, batching, culling, resource ownership, and profiler-visible render budgets |
| Video/cutscenes | Experimental | FFmpeg-backed playback when enabled |
| Networking | Experimental | Game-facing Lua sessions, authority, safe component snapshots, interpolation, spawn/despawn, diagnostics; optional ENet and TLS/DTLS transport |
| Android | Experimental | Shared action/touch controls, lifecycle/display services, and debug APK packaging; release workflow remains incomplete |
| Component metadata as sole source | Stable | Generated registry drives parsing, validation, schema export, Lua policy, and editor placeholders |
| Prefabs | Stable | Versioned entity/UI files, nesting, overrides, cycle diagnostics, CLI expansion/diff |
| Tree/layout runtime UI | Stable | Layout containers, controls, focus, themes, localization, safe-area roots, and virtual controls |
| Production text/dynamic UI | Experimental | Grapheme-safe editing, selection/caret geometry, SDL IME composition, wrapping, rich-text validation, runtime mutations, transactional parameterized UI prefabs, bounded virtualization ranges, localization, tweens, typed interaction events, and deterministic accessibility snapshots; complex shaping, fallback-font atlas pages, variable-height recycling, and native accessibility bridges remain incomplete |
| Tilemaps and sprite animation system | Stable | Runtime tile mutation, collision/navigation refresh, multiple tilesets, animations, object layers, clips, playback, and events |
| Deterministic replay/debug tooling | Stable | Versioned input replay, deterministic random state, profiling, and headless probes |
| Isometric placement/pathfinding | Stable | Grid conversion, occupancy, placement diagnostics, pathfinding, and rendering |
| Animation state machines | Stable | Cross-fades, 1D/2D blend spaces, layers/masks/additive metadata, explicit root motion, normalized time, events, preview state, and Lua control for 2D/3D players |
| Animation-timed 2D collision | Stable | Named receiver volumes and state-time windows with neutral overlap events; gameplay policy stays in scripts |
| Lightweight 3D animation adapter | Experimental | Shared state-machine control over embedded glTF clip names and deterministic `clip_N` aliases |
| Asset import and validation | Stable | Versioned importer metadata, source hashes, dependencies, stale-output and cycle diagnostics |
| General game data | Stable | Immutable schema-backed JSON assets, deterministic Lua queries, revisioned hot reload, and optional dialogue/quest/inventory modules |
| Portable asset packages | Stable | Deterministic dependency-complete export/import with checksums, path safety, licenses, and explicit conflicts |
| Linux cooking and packaging | Stable | Runtime-only deterministic cook output, manifest, Linux runtime bundle, and headless smoke coverage |
| Android cooking integration | Planned | Deferred until the Android runtime path has equivalent automated coverage |
| Functional editor | Planned | Begins after the authored data model stabilizes |
| High-end Unity-style 3D | Deferred | Not a product target |

The machine-readable status and compatibility workflow is documented in
[Capability Manifest and Reference Gates](capability-gates.md). The matrix
above remains a human summary; CI derives its contract from runtime binding
metadata, component descriptors, and the checked reference-gate document.
