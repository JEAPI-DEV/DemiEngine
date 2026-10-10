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
DEMI_HEADLESS=1 ./build/linux-release/demi test linux --project examples/colony_builder --timeout 180 --max-frames 24000
```

The E2E suite checks HUD resource progression, the idle camera, prefab references,
power loss, worker jobs and graph connectivity through the real runtime. Generated test reports stay under the ignored project `build/`.

Still to build: finished environment/building art, food production,
multiple colonists, day/night production and hazards.
The inherited starter scene is not the colony start scene.

The NVIDIA Vulkan Play/Stop crash found during this probe was corrected in
engine commit `db67794` by fixing Vulkan program/pipeline lifetimes. The
qualification record is `docs/editor-nvidia-hud-lifecycle-investigation.md`.
The project does not select a GPU or disable MSAA.

## Connected construction milestone

The project installs Orbit UI 1.1.0 and Ground Navigation 0.3.1, Inventory 1.2.1 and Character Needs 0.1.0 from the package
manager. Press Play: WASD pans, Solar array / Water extractor selects a plan,
and clicking a green footprint reserves metal and creates a construction site.
Engineer Mara walks around building footprints to an adjacent reachable cell,
hauls metal from the depot in loads of four, then builds over five seconds. No utility produces or consumes colony resources
while unfinished or disconnected.

Choose Connect, click two completed buildings, and the engineer constructs their
utility conduit for one metal. Connections are limited to 28 metres and reject
routes through other buildings or steep/blocked terrain. The starter habitat,
solar array and water extractor begin connected. A completed path back to the
habitat is required for power and water service. Disconnect removes the chosen
link; disabling a relay utility also disconnects its downstream branch.

Choose Cancel/Escape/right-click to leave placement mode. With no placement tool
active, select an unfinished site and use Cancel to release uncollected reservations. Delivered metal becomes a
recoverable crate; carried and delivered metal must return to the depot before
it can be reserved again.
The most recently queued site is already selected for this purpose. Finished
buildings are not demolished by Cancel. Disconnecting a pending link cancels its
job with the same physical recovery rules; completed links do not refund their spent metal.

The Colony Simulation component exposes worker speed, work duration, starting
metal, building references and the navigation extent. Resource demand is six kW
for the habitat plus two kW per connected enabled extractor; each connected solar
array provides twelve kW. This replaces the older colony-wide pool regardless of
connections. Grid extent defaults to 64 cells per axis at two metres per cell,
matching this terrain's 128 m size. No ground agent uses the old implicit grid API.

The reusable Ground Navigation package owns incremental terrain surveys, native
A* grids, obstacle clearance, replanning and character-controller movement. Game
modules under scripts/colony own the utility graph and construction queue. The
simulation coordinates these modules; job policy is not hardcoded in the engine.
Blocked routes wait and retry, disabled engineers pause, and collision stalls do
not complete a job. Surveying is bounded to 96 cells per fixed update.

The utility prefabs live under prefabs/utilities, and the engineer is an ordinary
prefab with a capsule and CharacterController3D. Construction checks all footprint
corners, clearance, terrain slope and at most 1.25 m height variation. Foundations
extend down to the sampled low ground. Each plan reserves its final building ID; scaffolds have separate temporary IDs
and are removed when finished. Cancelled IDs are not reused.

This is still a session-local vertical slice: one engineer, FIFO jobs, abstract
utility conduits, physical metal hauling and basic needs. Interior airlocks,
multiple-worker scheduling, crowd avoidance and saves remain future work.
Stopping Play resets runtime construction and resources. The inherited starter
scene is retained but is not the colony entry scene.

The E2E test covers resource failure/recovery, disabled workers, bounded walking,
queued costs/refunds, disconnected production, completed links, duplicate links,
transitive service, removal and insufficient materials.

## Hauling and habitat breaks

The depot holds metal and twelve meals. Mara carries up to four metal per trip;
construction only starts once the full cost is delivered. The HUD distinguishes
depot stock, reservations, cargo, site materials and recoverable materials.
A visible carried crate and cancellation recovery crates reflect those stocks.

Stamina, nutrition and suit oxygen decrease during activity. Mara interrupts work
for a habitat break when a need runs low; Rest engineer requests one immediately.
The habitat needs connectivity, power, water and oxygen for recovery. Rest uses
water, and meals restore nutrition. Cargo and construction progress survive the
break. Meal production and death/injury are not implemented.

Pause, 1x and 3x affect the simulation together. Camera panning remains available
while paused. The E2E test checks hauling capacity, pause, forced rest, partial
site cancellation, carried-metal cancellation and conservation throughout the
construction/connection sequence.

Ground routes now skip unnecessary grid waypoints along clear straight segments.
The survey grid is internal: Mara moves at arbitrary angles through open ground,
retaining required corners around blocked terrain and expanded building footprints.

## Reading the gameplay code

Start with `colony_simulation.lua`: it creates the services and orders their updates.
Then follow these modules by responsibility:

- `colony_construction.lua`: HUD actions, picking and construction commands.
- `colony/jobs.lua`: the FIFO construction queue and final prefab creation.
- `colony/worker.lua`: choosing tasks, travelling and performing arrived tasks.
- `colony/logistics.lua`: reservations, carried/site stock and cancellation recovery.
- `colony/needs.lua`: habitat and meal rules over the reusable needs meters.
- `colony/network.lua` and `colony_resources.lua`: utility links and production.

Reusable code lives in installed packages: Ground Navigation handles movement,
Inventory handles stock transfers, and Character Needs handles bounded meters and
thresholds. The example owns the colony rules. It does not import package-private
modules. Lua uses two-space indentation, separate statements and expanded control
flow; `.stylua.toml` records the format. Run StyLua on these modules when editing.
