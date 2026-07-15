# Capability Matrix

Status meanings:

- **Stable:** covered by automated tests and part of the supported workflow.
- **Experimental:** usable, but intentionally limited or still evolving.
- **Planned:** not yet a supported engine capability.

| Area | Status | Current scope |
|---|---|---|
| JSON projects/scenes | Stable | Versioned loading, stable IDs, CLI validation |
| Lua lifecycle and services | Stable | Lua 5.4, sol2, lifecycle, annotations, hot reload |
| 2D rendering | Stable | Sprite sheets/clips/events, sorting, camera follow, tilemaps, shapes, HUD, debug drawing |
| 2D physics | Stable | Box2D rigid bodies, box/circle colliders, filtering, triggers, queries, raycasts, and distance joints |
| HUD authoring | Stable | Flat versioned HUD elements and Lua mutation APIs |
| Saves | Stable | JSON slots, versions, migration hooks |
| Audio | Stable | miniaudio playback and entity audio sources |
| CLI validation and smoke tests | Stable | Project/example validation and headless runtime probes |
| Lightweight 3D | Experimental | Cycle-safe transform hierarchies, CLI-generated glTF box-collider assets, spatial queries, glTF materials and skeletal clips, deterministic batching, frustum culling, resource ownership, debug rendering, and profiled reference budgets |
| Video/cutscenes | Experimental | FFmpeg-backed playback when enabled |
| Networking | Experimental | Optional ENet, TLS/DTLS, Lua session helpers |
| Android | Experimental | Minimal client/server-oriented build support |
| Component metadata as sole source | Stable | Generated registry drives parsing, validation, schema export, Lua policy, and editor placeholders |
| Prefabs | Stable | Versioned entity/UI files, nesting, overrides, cycle diagnostics, CLI expansion/diff |
| Tree/layout runtime UI | Stable | Layout containers, controls, focus, themes, and localization |
| Tilemaps and sprite animation system | Stable | Layered/parallax tilemaps, generated collision, clips, playback, and events |
| Deterministic replay/debug tooling | Stable | Versioned input replay, deterministic random state, profiling, and headless probes |
| Isometric placement/pathfinding | Stable | Grid conversion, occupancy, placement diagnostics, pathfinding, and rendering |
| Animation state machines | Stable | Shared named states, transitions, parameters, timed events, and Lua control for 2D/3D players |
| Animation-timed 2D collision | Stable | Named receiver volumes and state-time windows with neutral overlap events; gameplay policy stays in scripts |
| Lightweight 3D animation adapter | Experimental | Shared state-machine control over embedded glTF clip names and deterministic `clip_N` aliases |
| Asset import and validation | Stable | Versioned importer metadata, source hashes, dependencies, stale-output and cycle diagnostics |
| Portable asset packages | Stable | Deterministic dependency-complete export/import with checksums, path safety, licenses, and explicit conflicts |
| Linux cooking and packaging | Stable | Runtime-only deterministic cook output, manifest, Linux runtime bundle, and headless smoke coverage |
| Android cooking integration | Planned | Deferred until the Android runtime path has equivalent automated coverage |
| Functional editor | Planned | Begins after the authored data model stabilizes |
| High-end Unity-style 3D | Deferred | Not a product target |
