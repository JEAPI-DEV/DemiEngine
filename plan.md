# DemiEngine Roadmap

DemiEngine is a deterministic, JSON-authored, Lua-first C++20 engine. Its
production focus is 2D, isometric/2.5D, and data-heavy/UI-heavy games.
Lightweight 3D is supported experimentally. High-end Unity-style 3D and hidden
editor-owned content are not product goals.

The authoritative implementation sequence and completion criteria live in
[`steps_to_be_taken_plan.md`](steps_to_be_taken_plan.md). Work proceeds in this
order:

1. Establish the documented product and compatibility contract.
2. Make component metadata and type-keyed world storage canonical.
3. Add deterministic prefabs and scene composition.
4. Build authorable tree/layout runtime UI.
5. Complete the reusable 2D gameplay foundation.
6. Formalize saves, deterministic simulation, profiling, and replay.
7. Add isometric strategy/builder systems.
8. Add animation and combat primitives.
9. Build a deterministic asset import/cook/package workflow.
10. Mature lightweight 3D using measured game requirements.
11. Stabilize the game-facing networking layer.
12. Build the editor on the stable data, diagnostics, and command layers.

## Current Implementation

The engine currently uses raylib, Lua 5.4, sol2, Box2D, miniaudio,
nlohmann/json, mbedTLS, optional ENet, and optional desktop FFmpeg/librsvg
integration. It does not use SDL3, bgfx, EnTT, Dear ImGui, or ImGuizmo.

Existing stable workflows include JSON project/scene loading, Lua lifecycle and
services, 2D rendering and physics, HUDs, saves, CLI validation, and headless
tests. Lightweight 3D, media, networking, and Android support remain
experimental.

See:

- [Architecture](docs/architecture.md)
- [Capability matrix](docs/capabilities.md)
- [Compatibility policy](docs/compatibility.md)
- [File formats](docs/file-formats.md)

## Delivery Rules

- Examples are engine probes, not places for one-off engine workarounds.
- Public data changes update schemas, migrations, documentation, and tests.
- Lua owns game rules; C++ owns stable systems and durable state boundaries.
- The CLI, runtime, validator, and future editor share data and diagnostics.
- Every milestone ends with build, test, affected-example validation, and a
  headless smoke test for new runtime behavior.
