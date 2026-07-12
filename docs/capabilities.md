# Capability Matrix

Status meanings:

- **Stable:** covered by automated tests and part of the supported workflow.
- **Experimental:** usable, but intentionally limited or still evolving.
- **Planned:** not yet a supported engine capability.

| Area | Status | Current scope |
|---|---|---|
| JSON projects/scenes | Stable | Versioned loading, stable IDs, CLI validation |
| Lua lifecycle and services | Stable | Lua 5.4, sol2, lifecycle, annotations, hot reload |
| 2D rendering | Stable | Sprites, shapes, cameras, HUD, debug drawing |
| 2D physics | Stable | Box2D rigid bodies, box colliders, contacts and overlap queries |
| HUD authoring | Stable | Flat versioned HUD elements and Lua mutation APIs |
| Saves | Stable | JSON slots, versions, migration hooks |
| Audio | Stable | miniaudio playback and entity audio sources |
| CLI validation and smoke tests | Stable | Project/example validation and headless runtime probes |
| Lightweight 3D | Experimental | Models, primitives, camera, animation, simple collision |
| Video/cutscenes | Experimental | FFmpeg-backed playback when enabled |
| Networking | Experimental | Optional ENet, TLS/DTLS, Lua session helpers |
| Android | Experimental | Minimal client/server-oriented build support |
| Component metadata as sole source | Stable | Generated registry drives parsing, validation, schema export, Lua policy, and editor placeholders |
| Prefabs | Stable | Versioned entity/UI files, nesting, overrides, cycle diagnostics, CLI expansion/diff |
| Tree/layout runtime UI | Planned | Layout containers, controls, focus, themes, localization |
| Tilemaps and sprite animation system | Planned | Milestone 4 |
| Deterministic replay/debug tooling | Planned | Milestone 5 |
| Isometric placement/pathfinding | Planned | Milestone 6 |
| Combat animation primitives | Planned | Milestone 7 |
| Deterministic asset cooking | Planned | Milestone 8 |
| Functional editor | Planned | Begins after the authored data model stabilizes |
| High-end Unity-style 3D | Deferred | Not a product target |
