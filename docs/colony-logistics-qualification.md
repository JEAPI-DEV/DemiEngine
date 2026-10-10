# Colony hauling and needs qualification

## Behavior and boundaries

The colony probe uses Inventory 1.2.0 for capacity-aware transfers and Ground
Navigation 0.2.0 for paths beginning at the actor's actual position and optional
travel-facing. Gameplay modules own reservations, hauling, needs and work policy.
The engine has no colony-specific rules.

One engineer carries four metal per trip from the depot placed through the native
editor. Building and link work require the full delivered cost. Cancelling a plan
releases only uncollected reservations; delivered stock becomes recoverable crates
and carried stock returns physically. The ledger asserts conservation across depot,
cargo, sites, recovery piles and consumed construction material.

Stamina, nutrition and suit oxygen trigger habitat breaks. Recovery requires a
habitable connected, powered habitat, water and oxygen; food consumes depot meals.
Breaks preserve cargo and work progress. Pause/1x/3x use the shared time service;
camera movement uses unscaled time. This remains one worker with FIFO construction,
no food production, injury/death, interior simulation, crowd avoidance or save system.

## Editor and tooling corrections

- Package refresh could throw from filesystem canonicalization during a package
  directory replacement and terminate the editor. Hash labels now use lexical
  paths; filesystem failures become diagnostics. A concurrent rename/read test
  covers this boundary. Existing package integrity validation remains in place.
- Scene canvas clicks and asset drops activate their document so Hierarchy and
  Inspector follow the destination even when a HUD document was active. The
  docking regression drags from Assets with a HUD focused and checks Undo.
- Newly placed prefab transform overrides use shared authored-value normalization
  rather than retaining float noise. Elevated terrain placement remains covered.
- `test linux` forwards window size, frame budget and profiler output arguments,
  validates options and rejects unknown flags. Its child-process test covers
  paths containing spaces and positional projects following options. Earlier
  standalone resolution claims are corrected in their original records.

## Verification

Focused CTests passed: editor drag authoring, editor shell docking, project
discovery, package manager and desktop test commands. Inventory has four passing
package tests; Ground Navigation has five. The project validates 95 reachable
files without diagnostics and installs its four public locked dependencies.

The source and cooked headless E2E scenario passes with `--max-frames 24000`
and `--timeout 180`. It covers resource failure/recovery, disabled workers,
bounded walking, multi-load delivery, pause, habitat recovery, both partial-site
and carried-load cancellation, graph service, duplicate connections, material
conservation and insufficient funds. The default 120-second headless simulation
budget is too short for the expanded scenario and correctly reports unfinished
work as failure.

Native screenshot `/tmp/colony-visible-hauling.png` shows a returning carried
load, status text and time controls without overlap. X11 window geometry was
measured at 5120 x 2806, rather than inferred from requested CLI arguments.
Both source and cooked visible E2E runs passed with the same measured geometry.
The cooked capture `/tmp/colony-cooked-hauling.png` shows the recovery crate.
Embedded editor Play also renders the updated HUD and depot correctly in
`/tmp/colony-logistics-editor-play2.png`, with the runtime hierarchy isolated
from authored data.

Inventory 1.2.0 and Ground Navigation 0.2.0 are published on demiengine.de;
all four dependency archives match staging. The navigation package directory
from the previous operator import lacked group-write permission; restoring it
allowed the HTTP publisher to create the new version. The store release
`20261010-131901-colony-logistics` passed Composer tests, Twig lint and cache warmup;
its health endpoint returned OK after the atomic switch.

## Readable gameplay modules and reusable needs (2026-10-10)

The colony scripts, Ground Navigation, Inventory and their tests now use expanded
Lua statements and control flow with a checked-in StyLua configuration. Worker
arrival actions are separate from travel/interrupt orchestration. The example
README maps the modules and explains which policies remain game-specific.
Inherited starter controller scripts were not reformatted.

Character Needs 0.1.0 extracts named bounded values, time-based rates and separate
low/recovery thresholds. It has no engine, HUD, inventory or scene dependency;
the colony adapter retains food transactions, water consumption, habitat checks
and manual rest. Ground Navigation 0.3.1 and Inventory 1.2.1 also ship readable
sources. All five locked dependencies are installed from the public registry,
and published archives match the staging registry.

Package tests passed: Ground Navigation 12, Inventory 4, Character Needs 4.
The source and cooked headless hauling/needs/connections scenario passed after
the extraction and formatting. The public packages guide and package READMEs
explain usage. Store deployment `20261010-134223-readable-gameplay` passed its
Composer checks, Twig lint and cache warmup; health returned OK after switching.
Native prefab-stage checks found and qualified the dependent-preview fix in
`editor-asset-authoring-qualification.md`.
