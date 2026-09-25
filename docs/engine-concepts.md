# Projects, entities, assets and packages

Start with a project (`demi.project.json`). It chooses the main scene, declares
package dependencies and holds game-wide settings. Use the editor for supported
visual authoring workflows and the CLI for validation, tests and builds. Both
read the same source files; editor layout and runtime Play state are not game
source.

## What goes where?

| Concept | Purpose | Example |
|---|---|---|
| Scene | A set of entities loaded together | `scene://game/main` |
| Entity | An object with a stable ID and components | A player, light or wall |
| Component | Data describing one capability on an entity | `Transform3D`, `MeshRenderer`, `Fracture3D` |
| Prefab | Reusable entity composition, with per-instance overrides | `prefab://doorway` |
| HUD / UI prefab | A screen-space interface / reusable UI subtree | `*.hud.json`, `ui-prefab://button` |
| Asset | An imported resource referenced by stable identity | `asset://textures/brick` |
| Package | Installable Lua modules or assets shared across projects | `demi.gameplay.health` |

Nest related entities using `children`. Use an explicit transform `parent` only
when nesting cannot describe the relationship, such as a cross-prefab reference.
IDs remain document-wide; nesting does not make a new ID namespace.

Assets contain resources; entities use them. A model asset does not itself have
health, collision or game logic. Add those components or scripts to the entity
that renders the model. Prefabs save that composition for reuse.

## Engine services and Lua packages

Import native services explicitly:

```lua
local Input = require("demi.input")
local Rigidbody = require("demi.physics.rigidbody3d")
```

The local variable name is your choice. Native services ship with the engine;
their Lua stub files describe the API for autocomplete and are not implementations.

Gameplay packages are optional Lua implementations. Install the package, then
require one of its declared public modules:

```lua
local Events = require("demi.gameplay.events")
local events = Events.new()
```

A package name is the installation identity. A public module name is an import;
one package can export several modules. The [package guide](../packages/README.md)
lists responsibilities and installation commands. Do not import another
package's private files.

## Source and generated data

Edit scenes, prefabs, scripts and asset manifests. Cooking turns those sources
into build data and records dependencies. Cache files, installed packages and
generated fracture geometry are derived data; do not maintain them by hand.
Saves use the platform's writable data directory, not the project source tree.

For destruction, `Destructible3D` owns an assembly. `Fracture3D` describes how a
mesh participates; `Masonry3D` describes repeated brickwork. Native physics owns
splits and collisions. Optional gameplay packages supply weapons and streaming
policy. See [fracture authoring](fracture-authoring.md) and
[streamed destruction](streamed-destruction.md).

## Where to continue

- [Getting started](getting-started.md): create and run a project.
- [Editor](editor.md): visual authoring, Play mode and workspace controls.
- [Capability matrix](capabilities.md): supported features and platform gaps.
- [Shipping](shipping.md): validation, cooking and release artifacts.
- [Compatibility policy](compatibility.md): breaking changes during alpha/beta.

Benchmark reports record specific runs. They do not establish a general capacity
guarantee for every scene or device.
