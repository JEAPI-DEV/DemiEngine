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
- `src/demi/runtime/app`: loop and subsystem orchestration plus the narrow 2D
  platform/render composition root.
- `src/demi/runtime/platform`: SDL3 window, lifecycle, clipboard, display, and
  input translation.
- `src/demi/runtime/scene`: project/scene/HUD models, registries, and loading.
- `src/demi/runtime/render`: backend-neutral renderer-facing systems. Visible
  2D and lightweight 3D rendering use bgfx. See `docs/bgfx-migration.md`.
- `src/demi/runtime/physics`: Box2D integration plus lightweight 3D collision,
  overlap, and raycast queries.
- `src/demi/runtime/scripting`: Lua lifecycle, services, annotations, and
  installable bindings.
- `src/demi/runtime/audio`, `media`, and `network`: isolated integrations.
  Networking keeps `NetworkSystem` at the transport/security boundary;
  validated `NetworkContract`, `NetworkOwnershipRegistry`, and
  `NetworkMessageGateway` services own policy and authority;
  `GameNetworkSession` retains compatibility identity/diagnostics, while
  `ReplicatedState` gates snapshots through contract and component metadata.

## Current Technology

- **SDL3:** Linux and Android windows, lifecycle, input, clipboard, and native
  window handles for 2D scenes.
- **bgfx:** active Vulkan-first Linux and Android 2D/3D renderer. Its lifecycle
  is isolated behind `GraphicsDevice`.
- **Lua 5.4 + sol2:** gameplay VM and C++ bindings.
- **Box2D 2.4.1:** 2D rigid bodies and collision.
- **miniaudio 0.11.22:** audio playback.
- **nlohmann/json 3.11.3:** authored and runtime JSON.
- **FFmpeg:** optional-at-configure system dependency for media, enabled by
  default on desktop builds.
- **mbedTLS 3.6.2:** TLS and DTLS security support.
- **ENet 1.3.18:** optional reliable UDP transport.
- **librsvg:** optional SVG rasterization when available.

There is no EnTT, Dear ImGui, or ImGuizmo dependency in the current
implementation.

## Data And Composition

Projects, scenes, HUDs, saves, and assets are versioned JSON. Stable IDs and
URI-style references are preferred over positional references. Generated data
is kept outside authored source directories.

Scene component classes own their stable JSON name, defaults, parser, Lua
exposure metadata, and dimensional domain. `ComponentRegistry` is the runtime
lookup path. `Entity` stores components in type-keyed `ComponentStorage`.

Runtime composition keeps three narrow owners: `RuntimePrefabService` expands
and pools authored prefabs, `SceneFlow` prepares and applies scene transitions,
and `ResourceLifetimeRegistry` records scene/persistent asset ownership. Lua
bindings adapt these services but do not duplicate prefab or scene parsing.

UI nodes are parsed into one tree-oriented model and resolved by the layout
engine before either renderer consumes them.

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

## Keeping Large Translation Units Cohesive

Line count is a warning, not an architectural boundary. Split a file when it
has multiple owners or reasons to change; keep dense domain algorithms together
when separating them would only add forwarding APIs.

The current refactoring order is:

1. `RuntimeApp.cpp`: keep it as the composition root and move reusable policies
   behind runtime services. Step 7 has already extracted initial asset
   preparation into `RuntimeAssetBootstrap` and atomic watched reload into
   `RuntimeAssetReload`. Later loop extraction should produce headless, 2D, and
   3D runners without introducing a second startup configuration object.
2. `BgfxRenderer3D.cpp`: asset decode/upload now lives in
   `BgfxRenderer3DAssets.cpp`, while shared render-asset file access and texture
   sampling live in `RenderAssetLoading`. Render-pass assembly is the next
   boundary if frame extraction grows. `Bgfx3DAppHost` remains the native
   lifetime owner; the renderer must not gain application or scene-flow policy.
3. `Physics2D.cpp` and `PhysicsWorld3D.cpp`: separate body/shape construction,
   simulation synchronization, contacts, and queries along existing domain
   seams. Do not split individual collision algorithms merely to reduce lines.
4. `LuaNetworkSessionBindings.cpp`: move session policy into runtime network
   services and leave topic-based binding installers as thin Lua adapters.
5. `Validation.cpp`: separate format-specific validators behind the existing
   shared diagnostic contract, without duplicating schema or parsing rules.

New functionality should follow these boundaries now; older files can migrate
incrementally when behavior changes provide focused regression coverage.

Small shared functions belong at the narrowest real boundary. CLI option lookup
is centralized in `CliArguments.h`; renderer binary access and sampling policy
are centralized in `RenderAssetLoading`. Neither is a general-purpose utility
namespace, and neither introduces a new owner or configuration object.

## Compatibility

All durable format changes follow the [compatibility policy](compatibility.md).
