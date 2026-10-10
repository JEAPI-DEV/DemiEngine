# Colony Builder editor probe

Experimental lightweight-3D colony prototype used to exercise native editor
workflows. This is an early blockout, not a complete Planetbase recreation.

Open `demi.project.json` in the editor. The start scene is `scene://colony`.
Click Play and focus Game View; WASD pans the camera. Stop returns to authored
state. The camera's Pan Speed is editable through its Lua component.

`Colony Simulation` owns the life-support prototype. Its Lua component exposes
solar output, power demand, water production and storage capacity. Its Solar
Array and Water Extractor entity references were assigned by dragging the
buildings from Hierarchy onto the Inspector fields. Defaults provide
4 kW surplus: water increases and oxygen rises toward 100%. Setting power
supply below demand stops water extraction and oxygen production; oxygen then
declines. Each Play starts with 25 L of water (bounded by capacity) and 25%
oxygen. Disabling the solar array removes its supply; disabling only the water
extractor stops extraction while oxygen production consumes stored water.
The dark-blue solar blockout is a linked prefab: conversion preserved its
existing ID and the simulation reference. The teal cylinder is the extractor.
HUD text updates at five times per simulated second. Values are
illustrative gameplay units, not a completed resource/network model.

Native editor checks covered script discovery/attachment, editable properties,
HUD updates and both powered/unpowered cases. To repeat the automated check:

```sh
DEMI_HEADLESS=1 ./build/linux-release/demi test linux --project examples/colony_builder
```

The E2E suite checks HUD resource progression, the idle camera, prefab reference
preservation, power loss, extractor shutdown and recovery through the real runtime. Generated test reports stay under the ignored project `build/`.

Still to build: finished environment/building art, connections,
colonist work/pathfinding, storage logistics, day/night production and hazards.
The inherited starter scene is not the colony start scene.

The NVIDIA Vulkan Play/Stop crash found during this probe was corrected in
engine commit `db67794` by fixing Vulkan program/pipeline lifetimes. The
qualification record is `docs/editor-nvidia-hud-lifecycle-investigation.md`.
The project does not select a GPU or disable MSAA.

## Construction milestone

The game now installs Orbit UI 1.1.0 through the public package manager. The
resource strip keeps the original resource IDs, and an anchored Orbit panel
provides icon buttons for Solar array (6 metal), Water extractor (8 metal), and
Cancel. Starting metal is 24; edit it on the Construction Lua component.

In Play, click a utility and point at the terrain. A green footprint means the
site is valid and affordable; red means it is rejected. Click to build, or use
Cancel, Escape or right-click to leave placement mode. WASD still pans the camera.
Buildings snap to a two-metre grid. Placement samples the actual terrain collider,
checks all footprint corners and building clearance, and rejects steep slopes,
terrain edges and more than 1.25 metres of height variation. Foundations extend
to the sampled low ground. Site sampling and preview updates are cached while
the snapped site is unchanged.

New solar arrays add 12 kW each. New extractors draw 2 kW and produce water at the
simulation's Water Production rate. Disabled constructed roots stop contributing.
This is still a single colony-wide supply model: physical utility connections,
construction jobs/hauling and autonomous colonists are the next gameplay steps.
Construction is immediate and lasts for the current Play session; saves are not
implemented yet. Stopping Play resets all runtime construction and resources.

The reusable build prefabs live under prefabs/utilities. Their IDs are stable, with
an unscaled base root and separate visual children; gameplay uses Prefab.instantiate
with a foundation override rather than duplicating engine mesh-generation code.
The camera was repositioned through the native Inspector to expose buildable
land above the toolbar. The E2E test covers costs, cancellation, occupied-site
rejection, prefab creation, production integration and insufficient-material
feedback through real HUD/world input.
