# Terrain generation quality qualification

Date: 2026-10-13. Build: `linux-release`, generator version 7.

## Production behavior

The shared generator and graph executor accept a transient Preview/Standard
work policy; the editor labels Standard as Final. Both retain the authored grid,
source noise and drainage. Preview reduces hydraulic iterations/droplets/walk
length and skips thermal relaxation. Zero hydraulic iterations now means zero
hydraulic work; Final thermal strength remains independent. Progress advances
inside hydraulic and thermal processing, with bounded cancellation checks.

Graph cache signatures separate quality-dependent erosion and its descendants
while reusing unchanged sources. Manual sculpt replay retains the generated
checkpoint and does not implicitly rerun erosion. The editor installs Preview
through reversible temporary publication, without committing the authored draft.
Discard restores the retained Final field across shared asset placements. Final
regeneration records history against the original Final field, with a separate
patch from the currently displayed Preview. Undo/Redo never restores Preview.
Save, Play, Apply, brush updates, production caches and cooked writing/loading
reject Preview results. The quality selection itself is not authored data.

Drainage accumulation now propagates in reverse discovery order, so parents
receive their complete upstream contributions before forwarding them. The
existing zero-at-border flow convention is retained. Thermal transport uses
Euclidean neighbor distances derived from both physical cell dimensions, fixing
diagonal and anisotropic slopes. Generator version 7 invalidates previous
results; authored sources remain unchanged and shipping content must be recooked.

Drainage exposes normalized Flow accumulation. Erosion publishes normalized
Deposited sediment and Eroded material pins, plus signed net height change in
its in-memory graph artifacts. The two masks normalize independently; an absent
sign produces zero. They can drive subsequent graph nodes and are not a new
cooked gameplay query. Source/grid ownership and cancellation remain shared.

## Validation

Thirteen focused suites pass: drainage, erosion, sediment masks, graph,
graph-landform controls, pipeline, generation cache, cooked terrain, editor
authoring, workspace, terrain assets, history and graph UI. Tests cover exact
chain/branch accumulation, sub-talus and anisotropic conservation/stability,
zero hydraulic work, in-stage cancellation/progress, distinct quality caches,
source immutability, signed output and downstream mask consumption. Changing
chunk partition leaves the tested generated heights unchanged.

Actual workspace tests exercise Preview publication, blocked Save/Apply,
Discard and selection rollback, shared placements, Final regeneration and
Final-only Undo/Redo. Cook writer and decoded/load guards reject Preview.
Two test fixtures were corrected to current contracts: brush strength within
[0,1] and palette format 2 with prefab references outside asset dependencies.

All three example projects validate: rocky_valley_3d (11 files),
terrain_graph_3d (15) and terrain_3d (26). A three-frame headless rocky-valley run
passes. Release runtime/editor were rebuilt. No new desktop mouse interaction,
physical Android rendering or website deployment is claimed.

## Representative generation probe

Build `demi-terrain-generation-benchmark` explicitly, then run:

```sh
./build/linux-release/demi-terrain-generation-benchmark examples/rocky_valley_3d/assets/terrain/landscape.terrain.json --geometry-only --repeat
```

This is CPU geometry generation with palette/scatter preparation explicitly
excluded, not a whole-editor or frame-rate benchmark. The original 500-by-500
cell grid and 2,000-by-2,000 extent remain unchanged (251,001 samples). Two
sequential runs per quality, with no other engine instance/build/tests active:

| Mode | Generation times | Height digest, identical on repeat |
| --- | --- | --- |
| Preview | 396.5 / 346.5 ms | `bcb42c5a6cdcb3a5` |
| Final | 873.7 / 837.0 ms | `1203cc2bd175d33f` |

Preview reported 19 progress callbacks and Final 3,953. Both retained signed
transport data. Report: `/tmp/terrain-milestone3-generation.json`. This proves
original-grid reduced work and same-build repeatability, not cross-platform
bitwise determinism or superior visual quality to an upstream solver.

## Remaining milestone 3 work

The [library survey](terrain-erosion-library-evaluation.md) is source-level;
a representative upstream solver comparison remains open. The priority-flood
downstream tree can rise on original terrain when escaping a depression, so it
is not yet a visible river route. Explicit sea/edge/closed-basin endpoints and
sloped river surface/carving policy are needed. The visual acceptance gate and
terrain/water ordering qualification are not complete. Shadow work is deferred
at the user's request. This checkpoint does not complete milestone 3.
