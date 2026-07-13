# Architecture

DemiEngine is a Linux-first C++20 runtime built around deterministic JSON
authoring and Lua gameplay. The CLI, runtime, validation path, and future editor
consume the same project and scene model.

## Product Tiers

- **Production focus:** 2D, isometric/2.5D, and data-heavy or UI-heavy games.
- **Experimental support:** lightweight 3D exploration, puzzle, and small action
  games.
- **Explicitly deferred:** high-end Unity-style 3D rendering, terrain tooling,
  visual scripting, shader graphs, and editor-only content formats.

See [the capability matrix](capabilities.md) for the current status of each
subsystem.

## Runtime Layers

Dependency flow is intentionally one-way:

1. CLI/runtime entry points coordinate application services.
2. Runtime systems consume the scene world and narrow subsystem APIs.
3. Scene components own authored data, defaults, metadata, and JSON parsing.
4. Lua binding adapters depend on runtime services; scene components do not
   depend on sol2 or Lua.
5. Platform and third-party integrations stay behind their runtime subsystem.

The current source layout reflects those boundaries:

- `src/demi/assets`: asset manifests and registry.
- `src/demi/diagnostics`: shared structured diagnostics.
- `src/demi/filesystem`: deterministic project path resolution.
- `src/demi/schema`: source validation.
- `src/demi/runtime/app`: window, input, loop, and subsystem orchestration.
- `src/demi/runtime/scene`: project/scene/HUD models, registries, and loading.
- `src/demi/runtime/render`: raylib-backed 2D and lightweight 3D rendering.
- `src/demi/runtime/physics`: Box2D integration plus lightweight 3D collision,
  overlap, and raycast queries.
- `src/demi/runtime/scripting`: Lua lifecycle, services, annotations, and
  installable bindings.
- `src/demi/runtime/audio`, `media`, and `network`: isolated integrations.

## Current Technology

- **raylib 5.5:** windows, input, 2D/3D drawing, textures, and models.
- **Lua 5.4 + sol2:** gameplay VM and C++ bindings.
- **Box2D 2.4.1:** 2D rigid bodies and collision.
- **miniaudio 0.11.22:** audio playback.
- **nlohmann/json 3.11.3:** authored and runtime JSON.
- **FFmpeg:** optional-at-configure system dependency for media, enabled by
  default on desktop builds.
- **mbedTLS 3.6.2:** TLS and DTLS security support.
- **ENet 1.3.18:** optional reliable UDP transport.
- **librsvg:** optional SVG rasterization when available.

There is no SDL3, bgfx, EnTT, Dear ImGui, or ImGuizmo dependency in the current
implementation.

## Data And Composition

Projects, scenes, HUDs, saves, and assets are versioned JSON. Stable IDs and
URI-style references are preferred over positional references. Generated data
is kept outside authored source directories.

Scene component classes own their stable JSON name, defaults, parser, Lua
exposure metadata, and dimensional domain. `ComponentRegistry` is the runtime
lookup path. The current `Entity` typed optional storage is transitional;
Milestone 1 replaces it with type-keyed storage.

HUD element classes similarly own their type name and parsing behavior.
`HudParser` only reads the document and dispatches through the HUD registry.

## Lua Boundary

Lua owns game rules. C++ owns stable systems and serialization boundaries.
Bindings are small installable modules grouped by responsibility. Binding code
lives in the scripting layer so scene/domain types do not import sol2.

Supported lifecycle functions are `on_create`, `on_start`, `on_update`,
`on_fixed_update`, and `on_destroy`.

## Validation And Editor Direction

`demi validate` is the current diagnostics contract. Validation, serialization,
component metadata, schemas, Lua stubs, and the future inspector must converge
on the same metadata path during Milestone 1.

The editor executable is currently a boundary, not a functional editor. Editor
work starts after components, prefabs, UI layout, saves, and command semantics
are stable. It must never create hidden state that the CLI cannot validate.

## Compatibility

All durable format changes follow the [compatibility policy](compatibility.md).
