# Regional terrain streaming and unified authoring

Target agreed 2026-10-24: an approximately 240 x 220 km (52,800 km2)
procedural Alpine-inspired region, not a reconstruction of real geographic data.
Include lowlands, foothills, connected ranges and valleys. World dimensions,
origin, seed, landforms and vegetation are authored data, never a built-in demo.

## One authoring contract

An ordinary Terrain asset owns TerrainRecipe, TerrainGraph, biome rules, edits
and the existing prefab palette. A small terrain_region DataAsset references
that asset and supplies regional placement/sampling. It must not introduce a
second graph or a regional-layer language. The Assets panel opens the referenced
Terrain through the existing terrain workspace. The example references
asset://terrain/regions/alpine_terrain; edit its normal .terrain.json source to
change mountains, valleys, seed or size. The graph's paths remain terrain-local
coordinates; regional placement adds the authored origin.

Source loading and CLI validation share native parsers. Camera-driven regional
terrain and nearby procedural vegetation now run through the same services in
standalone and embedded Play. The Terrain asset dependency is retained as a
program, not eagerly generated as a world-sized field. Prototype resource
preparation includes each prefab's material and model LOD references once.
Cooked program transport and floating-origin gameplay remain open.

The ordinary graph editor now opens a regional source as an overview at its
authored preview resolution, with an empty-placement copy of the resolved
palette. It preserves material bindings, graph identity and the source palette;
it never writes that overview into the final finite cache. Detailed scattering
runs only for runtime cells.

## Verified foundation and remaining feature matrix

| Feature | Finite terrain | Regional consumer |
| --- | --- | --- |
| Noise/constant/landform sources | Existing | Shared source evaluator and seed rules |
| Elevation/path/polygon masks, height blend/offset | Existing | Shared point kernels; finite parity tested |
| Fixed-footprint global samples/normals | Finite grid | Neighbor and coarse/fine shared-sample tests |
| Slope-dependent graph operations | Existing | Needs dependency-aware tile halos |
| Biome rules and prefab scatter | Existing | Elevation/slope rules; final-field palette scatter; async prepare/adopt |
| Erosion/drainage/water | Existing | Needs coordinated regional or supertile preparation |
| Sculpt/brush override persistence | Existing | Needs cell ownership and replay/load integration |
| Physics and render cell residency | Whole finite world plus render-resource retirement | Camera-driven balanced cover, async mesh/shape preparation, scene adoption and eviction |

Unsupported regional nodes and connected outputs fail with useful diagnostics;
they are not silently ignored or evaluated independently per tile. The regional patch path stitches odd edge samples to the adjacent coarser
edge; its 2:1 balance and continuous collision cover have native tests.

## Implementation sequence and acceptance

1. **Shared graph execution:** use the same node definitions, parsers, parameter
   defaults, seed derivation and mathematical kernels. Compile only the requested
   dependencies. Pointwise nodes evaluate directly; stencil nodes expand input
   halos. Regional simulations use an authoritative prepared region/supertile
   result. Never run disconnected erosion on neighboring loaded cells and call
   their boundaries equivalent.
2. **Global vegetation candidates:** stable world-coordinate candidate keys and
   a spacing halo make load order and cell boundaries irrelevant. Environment
   sampling must come from shared regional height/water/biome authority. Local
   finite behavior and its existing keys remain unchanged.
3. **Bounded lifecycle:** reserve memory before worker dispatch; own cancellation
   and stale-result handling; publish cells on the main thread under a budget.
   Actual eviction releases field, geometry, instance and collision ownership.
   Runtime and editor use the same service, rather than separate implementations.
4. **Distant coverage:** hierarchical coarse terrain remains while detailed cells
   load. Test 2:1 boundaries and replace coverage atomically. Nearby collision is
   independent of horizon visibility. New spawns/teleports wait for collision
   readiness; they must not fall through an unfinished cell.
5. **World precision and persistence:** stable global cell coordinates plus local
   render/physics coordinates. Rebasing must preserve hierarchy, physics state,
   queries, saved overrides and deterministic identifiers. Current Jolt build is
   single precision; merely increasing the camera far plane does not solve this.
6. **Native qualification:** traverse, reverse, teleport and revisit many cells
   over the whole authored extent. Verify bounded CPU/GPU/in-flight memory,
   stable IDs and border geometry, actual body/resource eviction, loading tails
   and collision readiness. Record build configuration and complete-scene memory,
   not just the height texture counter.

## Vegetation storage decision

The requested choices are compact stored placements and deterministic procedural
regeneration. A setting that regenerates both existing full vectors at load would
save storage but not their steady-state RAM, so it is not sufficient by itself.
A procedural source must retain exact generation provenance and rebuild the
original candidate universe for edits/undo; visible transforms still require
memory while in use. Player removals and hand-authored overrides remain saved.

Do not regenerate from an arbitrary graph's final heightfield: Scatter Palette
may occur before a later erosion or water operation. Preserve its exact upstream
input, wet-coverage distinction, constraints, palette, seed and algorithm version,
or diagnose unsupported configurations. Native region cells can later use the
same replayable source abstraction for stored records and procedural candidates.

### CPU evidence: Linux Release, 2026-10-24

`demi-terrain-scatter-benchmark` uses prepared 2 m surfaces, jittered/clustered
mixed vegetation, slope/moisture/water constraints, spacing and spatial
exclusions. Two warmups and nine timed repetitions, alternating path order.
Every result is checked for exact fields and stable identity outside timing.
Surface generation, disk IO, GPU upload and instance publication are excluded
from both paths. Regeneration includes environmental constraint construction.

| Cell | Placements | Regenerate median / p95 | Decode median / p95 |
| --- | ---: | ---: | ---: |
| 128 m | 7,170 | 10.27 / 11.63 ms | 1.24 / 1.77 ms |
| 256 m | 28,713 | 39.21 / 40.46 ms | 3.73 / 3.95 ms |
| 512 m | 108,228 | 148.78 / 153.85 ms | 10.44 / 14.21 ms |

These are final-placement-only comparisons. Retaining editor candidates is also
benchmarked: 512 m regeneration median149.50 ms versus decoding20.19 ms. Reported
allocation counters count C++ requests and requested bytes, not RSS or peak live
memory. The measured native placement struct is176 bytes before its string heap;
compact stored records remain40 bytes plus shared definitions.

Procedural placement is a candidate for background prefetch, not work to do every
frame. Procedural generation remains the intended streamed-world default if
measurements support it; the first slower result is not grounds to abandon it.
Optimize and qualify native end-to-end worker/publication/readiness and RAM
behavior before making that decision. No storage toggle or default change is
claimed yet.

Reproduce:

```sh
cmake --build --preset linux-release --target demi-terrain-scatter-benchmark
./build/linux-release/demi-terrain-scatter-benchmark > /tmp/scatter.csv
```

Do not run a build or another engine instance during measurement. The benchmark
is excluded from the default build and does not open a window or alter assets.

### Scatter optimization follow-up

Linux Release, 15 timed repetitions per case, same prepared surface and rules.
The shared scatter path now precomputes rule seed prefixes, rejects losing
spacing neighbors before computing their positions, reuses candidate positions,
and retains the four cluster-noise corners for the last visited cluster cell.
The latter is bounded stack state per rule/call, with bitwise coordinate keys
that preserve signed zero. No placement output is cached globally.

| Cell | Before median | Optimized median / p95 | Placements |
| --- | ---: | ---: | ---: |
| 128 m | 9.29 ms | 6.14 / 6.27 ms | 7,170 |
| 256 m | 35.34 ms | 23.36 / 26.53 ms | 28,713 |
| 512 m | 132.99 ms | 79.21 / 80.64 ms | 108,228 |

Counts and stable identity digests match all pre-optimization cases, including
editor checkpoint cases. Each run also verifies regenerated versus decoded
placement fields exactly. Scatter and canonical seed tests pass. Timing varies
between runs; these are isolated CPU measurements, not a native streaming
frame-time guarantee. Decode of the optimized 512 m case measured 10.30 ms.

Use `--phases` to collect separate constraint preparation, placement loop and
editor checkpoint-copy samples. Before the corner reuse change, the 512 m phase
run measured about 4.25 ms preparing constraints versus 86.43 ms placing objects.
Afterward the placement phase measured 73.12 ms. Phase runs are separate and
exclude teardown; their medians are not additive. The full regeneration row is
the combined-cost measurement.

The global scatter domain is also implemented and tested against partitioned
cells: world candidate keys, half-open ownership and spacing neighbors preserve
placements across cell boundaries and load order. Callers must supply complete
shared wet/environment constraints and a sufficient field halo. The native residency service described below now supplies load-once retention
and eviction. A regional source consuming this domain and the authored
stored/procedural toggle remain integration work.

### Vegetation cell residency, Linux Release, 2026-10-24

`TerrainVegetationResidency` now owns asynchronous CPU cell placements using
`JobSystem`. Stored loading and procedural generation use the same source
callback and output contract. Keys include signed cell coordinates, source
revision and storage mode. The caller's revision must cover the exact graph
input, palette, overrides and generator version; the service does not guess
whether two different sources are equivalent.

Repeated demand reuses resident output. Removing demand retires it; revisiting
loads/generates again. A source error or truncated output is terminal for that
request, rather than retried every update. Changed revision/mode or absent demand
cancels pending work; its result is rejected even if the source ignores
cancellation. Shutdown joins jobs before releasing their inputs.

Preparation is admitted against an explicit scratch-plus-output reservation.
Actual retained vector/string capacities must fit that reservation. Retired
placements are destroyed on a worker to avoid thousands of string frees on the
main thread, and remain charged until cleanup completes. Ready, pending and
retiring payload are reported separately. This accounting excludes shared
source data, allocator metadata and fragmentation; it is not process RSS or a
hard OS memory limit. Sources remain responsible for conservative scratch
estimates and bounded allocation. Consumers must retire their own render and
collision ownership when the corresponding cell leaves demand.

The `--residency` probe uses one worker and a moving three-cell window, forward,
reverse and revisiting, with three repetitions. Each logical cell uses the same
prepared scatter fixture: this tests lifecycle, not real regional geography.
Each mode performed 27 loads. Thirty steady updates at each stop performed no
additional loads; exact placement comparisons passed after every revisit.

| Cell | Procedural worker mean | Update p95 / max | Peak resident payload |
| --- | ---: | ---: | ---: |
| 128 m | 5.41 ms | 0.0033 / 0.0100 ms | 5.75 MiB |
| 256 m | 20.40 ms | 0.0034 / 0.0148 ms | 23.01 MiB |
| 512 m | 78.89 ms | 0.0015 / 0.0133 ms | 90.50 MiB |

Before moving retirement onto workers, the 512 m maximum main-thread update was
6.89 ms. Stored 512 m decoding averaged 11.27 ms on the worker after that change.
Both modes returned owned resident and retiring payload to zero after the final
clear/drain. Each admitted job reserved 128 MiB in this probe, separately from
resident payload; this is a conservative probe reservation, not measured scratch
usage. Decode and generation retain different vector capacities. Source data,
terrain sampling, disk IO, rendering and collision publication are excluded.
The update maximum is a measured sample maximum, not a guaranteed latency bound.

```sh
cmake --build --preset linux-release --target demi-terrain-scatter-benchmark
./build/linux-release/demi-terrain-scatter-benchmark --residency --repetitions 3
```

`demi-terrain-vegetation-residency-tests` covers stored/procedural exact parity,
steady reuse, eviction/revisit, cancellation while a loader cannot stop, stale
revision rejection, admission and output budgets, terminal failure, truncation,
duplicate demand, publication throttling and destruction with active jobs.

This is a shared native CPU owner exercised by tests and the probe. The scene
publication bridge described below now consumes it; automatic camera-driven
scene traversal is still missing. The authored
storage toggle remains pending that integration; no ineffective JSON setting
has been added. Regional environment preparation and exact graph replay are
still prerequisites to a general source loader. The default decision remains
open pending those end-to-end measurements.

### Scene publication boundary, Linux Release, 2026-10-24

`TerrainVegetationWorld` connects the CPU residency service to the existing
scatter batch and runtime prefab publishers. Each cell gets a native transform
owner under its region owner. Its stable ID includes cell coordinates, but not
storage mode or source revision. Visual placements still use MeshInstances3D;
colliding/gameplay prefabs retain the ordinary RuntimePrefabService ownership.
The owner supplies terrain-local cell origins; this is not world-origin rebasing.

The scatter publisher now accepts a borrowed `TerrainScatterView`, so a cell
can publish its resident records without copying them into a temporary
HeightField. Finite terrain uses the same path through its existing overload.
Pending cell owners are snapshotted before queuing more commands, avoiding
pointers invalidated by command-buffer growth. Generated direct models and
prefab descendants inherit scene ownership as render batches already did.

Publication runs only for a changed cell revision or local origin. A resident,
unchanged cell is not rebuilt each update. Scene-level scatter exclusions are
passed through the same existing filtering logic; changes to overrides must
participate in the caller's source revision. A failed source/prefab preparation
leaves the previous published cell intact. Prefab bookkeeping is staged until
source validation succeeds; this does not promise rollback after allocation
failure inside generic command-buffer flushing.

Eviction removes only that cell's render batches and releases its ordinary
prefab instances, then removes the native cell owner. Disabling/removing the
region owner evicts its cells. Ordinary physics synchronization subsequently
removes their Jolt bodies. Reserved-ID collisions with unrelated entities fail
without replacing the existing entity. Other cells and unrelated entities are
preserved.

`demi-terrain-vegetation-world-tests` verifies two neighboring cells, mixed
visual batches and sphere-collider prefabs, stable IDs on revisit, steady reuse,
exclusions, failed replacement rollback, changed origins, disabled/deleted
owners and reserved-ID collision. Native Jolt overlap queries verify that an
evicted cell's collider disappears while its neighbor remains, and that revisit
restores collision. Tests also verify prefab bookkeeping and scene ownership.
Five focused Release suites pass: vegetation world/residency, scatter runtime,
terrain world and terrain world update.

Remaining integration: the scene component/loop still does not drive this
bridge from camera demand. The regional source loader must prepare/replay
correct graph/environment inputs, with cooked sources and render/physics
readiness coordinated before exposing the storage toggle. Native tests exercise
real World and physics publication, but there is no rendered regional traversal
qualification or GPU-resource retirement measurement yet. Earlier microsecond
residency timings exclude this new scene publication cost.

### Publication cost and shared engine fixes, Linux Release, 2026-10-24

The earlier residency update timings do **not** approximate complete cell arrival.
`demi-terrain-publication-benchmark` now measures worker loading, scene publication,
first native physics step, request-to-physics-ready latency, steady updates, and
scene/physics eviction separately. It asserts published render-instance/body
counts and complete eviction. It uses the normal generator, prefab publisher,
World commands and Jolt; terrain meshes, asset mesh decoding, GPU upload/drawing
and full Alpine traversal are excluded.

The fixture is a finite 2 m sampled rolling surface, collision-free grass prefab
instances and colliding sphere-tree prefabs. Test-only dimensions/densities are
not engine defaults or Alpine geography. The process warms prefab templates and
physics before five timed repetitions per storage mode. Worker count is one;
request-to-ready includes 1 ms polling. CPU: AMD Ryzen 7 6800H, Linux Release,
reported governor `powersave`. No other engine instance or build ran during
measurements. With five samples, the reported p95 equals the observed maximum;
these are diagnostic comparisons, not a sustained tail-latency qualification.

| Cell | Placements / colliders | Procedural scene publication before | After | Physics arrival after | Scene eviction before / after |
| --- | ---: | ---: | ---: | ---: | ---: |
| 64 m | 2,729 / 10 | Not measured | 3.48 ms | 0.06 ms | Not measured / 0.21 ms |
| 128 m | 9,386 / 50 | 25.12 ms | 13.29 ms | 0.17 ms | 1.01 / 1.20 ms |
| 512 m | 144,270 / 601 | 638.59 ms | 269.39 ms | 2.11 ms | 66.10 / 37.95 ms |

After changes, 512 m procedural worker generation measured about 54 ms median,
request-to-physics-ready was 327.73 ms, and steady scene-update-plus-physics was
0.47 ms median. Stored 512 m loading measured 9.56 ms median but still required
272.52 ms for publication: pre-storing placements does not remove this cost.
The visual-only comparison before optimization still took about 637 ms to
publish, confirming that native physics was not the dominant arrival cost.

Added profiler scopes `Terrain.scatter_batches`, `Terrain.scatter_entities` and
`Terrain.cell_commit` expose this work through the existing runtime profiler.
The initial profiled 512 m stored run showed about 398 / 168 / 66 ms respectively.
After changes it showed about 240 / 32 / 1.4 ms. Scope medians are not additive.

Shared changes, applied beyond the streamed example:

- MeshInstances3D copies share unchanged transform storage. Mutation detaches,
  preserving value semantics, stable labels, serialization and renderer revision
  invalidation. World migration no longer deep-copies every unchanged instance.
- Scatter batches reuse source/rule/part setup rather than reconstructing batch
  IDs per placement. An input-index bitmap tells entity reconciliation which
  placements are already handled, avoiding duplicate string sets and ID work.
- Consecutive WorldCommandBuffer deletes compact entities and rebuild lookup
  once. Mutation events preserve requested order, duplicate-delete semantics,
  and interleaved create/component/enable operations.
- The vegetation World bridge now belongs to the runtime library, alongside
  scatter publication, rather than creating a core-to-runtime static-link cycle.

Instance-copy/move/edit tests, command interleaving tests, full-exclusion batch
retirement, and the existing scatter/physics publication tests cover these paths.

Reproduce (sequentially, with no concurrent build):

```sh
cmake --build --preset linux-release --target demi-terrain-publication-benchmark
./build/linux-release/demi-terrain-publication-benchmark 64 5
./build/linux-release/demi-terrain-publication-benchmark 128 5
./build/linux-release/demi-terrain-publication-benchmark 512 5
./build/linux-release/demi-terrain-publication-benchmark 512 5 visual
```

**Alpine integration consequence:** the current whole-cell publisher is not
frame-budget safe for dense large cells. Background generation/prefetch alone
cannot solve main-thread publication. Use independently configurable vegetation
cell granularity and terrain horizon LOD coverage; 64 m is a measured probe, not
an engine limit or qualified default. Next work should separate immutable visual
batch preparation from main-thread adoption, budget publication/retirement, and
coordinate collision readiness with camera/gameplay demand. These must remain
shared runtime mechanisms consuming the same authored graph and palette.

### Research: open-world streaming architectures, 2026-10-24

Primary developer sources support separating visual distance, data readiness and
simulation activation. No authoritative public description of Elden Ring's full
streaming implementation was established in this research; do not attribute the
following systems to FromSoftware.

- Guerrilla's [Streaming the World of Horizon Zero Dawn](https://d3d3g8mu99pzk9.cloudfront.net/AstridHuntjens/Streaming%20the%20world%20of%20Horizon%20Zero%20Dawn.pdf)
  describes runtime asset conversion, shared dependency ownership, asynchronous
  loading/initialization, separate activation, and several terrain detail tiers
  including always-loaded very-low-detail coverage (slides 11, 16–19, 35–39).
- Sucker Punch's [Zen of Streaming](https://media.gdcvault.com/GDC%2B2021/ghost_streaming_gdc2021.pdf)
  describes compile-time flattening, compact GPU instances and grass fallback
  textures. The developer slides were read through this [PDF mirror](https://archive.thedatadungeon.com/ghost_of_tsushima_2020/documents/gdc_2021/ghost_streaming_gdc2021.pdf).
  Its 24–32-byte instance record is a render representation, not equivalent to
  our full placement provenance/gameplay record; the useful lesson is keeping
  those responsibilities separate.
- Epic documents [HLOD proxies for unloaded cells](https://dev.epicgames.com/documentation/unreal-engine/world-partition---hierarchical-level-of-detail-in-unreal-engine)
  and distinct [streaming/registration/unregistration budgets](https://dev.epicgames.com/documentation/unreal-engine/streaming-settings-of-the-unreal-engine-project-settings).
- Guerrilla's [GPU procedural placement presentation](https://www.guerrilla-games.com/read/gpu-based-procedural-placement-in-horizon-zero-dawn)
  demonstrates that artist-authored placement graphs and procedural runtime
  population can share one workflow. Runtime generation does not require
  abandoning authored graph control.

Application to DemiEngine is an engineering inference, not a claim that these
engines use our implementation or that their performance transfers directly:

1. Keep one graph/palette authoring contract; compile/cache its supported runtime
   dependencies. Stored and regenerated cells should produce the same compact
   prepared output, without repeating authoring-level object construction.
2. Add render-ready instance pages with shared prototypes, stable candidate
   identity and compact transforms. Keep editing provenance and interactive
   prefab entities at their appropriate owners. Existing draw batching is not
   sufficient while page construction still occupies the main thread.
3. Separate distant visual coverage from local gameplay/collision residency.
   Coarse terrain, vegetation proxies and matching far-ground materials must
   stay visible while finer representations arrive. Independent granularity
   does not require duplicate scene models or separate authoring languages.
4. Split loading, preparation, GPU upload, activation and retirement into
   cancellable stages. Bound the main-thread stages and retain fallback coverage
   until replacements are ready. A time budget cannot interrupt an indivisible
   269 ms publication call; that call must first become separable work.
5. Prefetch from camera/player demand and travel intent. Treat teleports and
   collision readiness explicitly. Measure upload and actual rendered traversal
   in addition to the CPU publication probe before choosing defaults.

Startup preloading can hide initial work but cannot solve sustained traversal,
activation or unloading spikes. Smaller cells remain a scheduling choice, not
the principal substitute for the runtime representation and lifecycle above.

### Worker-prepared visual publication, Linux Release, 2026-10-24

Stored and procedural cell loads now share an optional preparation stage in
`TerrainVegetationResidency`. `TerrainVisualPreparation` runs the existing
finite scatter batch builder using a private World and prefab service. The
composition root supplies the project directory; workers never borrow the live
prefab service. Existing native clients without a project context retain the
complete normal publisher for prefab records, rather than incorrectly turning
visual prefabs into individual entities.

The immutable output owns prepared visual batch entities and shared
MeshInstances3D transform storage. Handled visual placement records are discarded
on the worker. Interactive/unbatchable records remain available for the ordinary
main-thread prefab reconciliation, preserving live gameplay state and failure
rollback. No second graph, palette syntax or Alpine-specific placement rule was
introduced. This prepares the existing representation; packed GPU instance pages
are still a separate follow-up and are not claimed here.

The live World adopts visual batches without reconstructing per-instance IDs or
transforms. Evicted scene batches release their references before CPU residency
retires the prepared payload, leaving ordinary last-owner cleanup to workers.
Prepared payload is included in reservation accounting. The byte counter covers
instance/geometry/string payload, not process RSS, ECS registry overhead,
authoring metadata, shared asset resources or allocator fragmentation.
Replacement of an older visible revision and per-owner exclusions can still
incur main-thread copy/destruction work; those cases need publication budgeting.

The runtime profiler has thread-local `ScopedProfileSuppression` so preparation
jobs can reuse profiled code without reading or modifying the main-thread frame
capture. Explicit load/preparation timings are reported by cell residency.
Batch construction checks cancellation during placement iteration; stale and
cancelled results retain their reservation until completion as before.

Same finite CPU publication probe: one worker, five timed repetitions following
a warmup, main-thread prefab cache warm, private worker context per cell load.
Terrain mesh/collision creation, asset-mesh decoding, GPU upload/drawing and
rendered Alpine traversal remain outside this probe. Phase medians are not additive.

| Cell | Placements / colliders | Scene publication before / now | Physics arrival now | Worker visual preparation | Scene eviction before / now |
| --- | ---: | ---: | ---: | ---: | ---: |
| 64 m | 2,729 / 10 | 3.48 / 0.62 ms | 0.06 ms | 2.84 ms | 0.21 / 0.036 ms |
| 128 m | 9,386 / 50 | 13.29 / 3.24 ms | 0.16 ms | 9.68 ms | 1.20 / 0.13 ms |
| 512 m | 144,270 / 601 | 269.39 / 33.29 ms | 1.91 ms | 244.25 ms | 37.95 / 2.33 ms |

For the 512 m procedural case, worker generation was 53.77 ms, main-thread
arrival including physics was 35.34 ms, and request-to-physics-ready was
334.62 ms. The visual adoption scope itself was 0.012 ms; interactive entity
reconciliation accounted for about 32.20 ms. Both storage modes reported
27,517,301 bytes of accounted retained cell payload. This moves substantial
work out of the frame; it does not eliminate that work or make dense large-cell
activation frame-budget safe. The next limiting stage is interactive activation.

Regression coverage adds finite/prepared visual parity for hierarchy transforms,
LOD/shadow metadata, exact instance labels/transforms, cancellation, prepared
payload budget rejection and worker retirement. Native scene/physics, ordinary
scatter, renderer and profiler suites remain required gates. Automatic regional
camera demand, cooked regional sources, compact render storage, bounded
interactive activation and full traversal qualification are still unfinished.

### Prepared prefab instances and frame-paced qualification, 2026-10-24

`RuntimePrefabService::prepareInstance` now expands the prefab, parses native
components, resolves initial hierarchy/position data, and returns an immutable
unpublished snapshot. It is synchronous on its calling thread and can run on a
worker-owned service. It does not register live ownership, start Lua callbacks,
or create physics bodies. `adoptPrepared` checks the normalized project root,
instance ID, live entity IDs and queued IDs before queuing native entities and
registering ownership. Validation failures leave commands/ownership unchanged;
allocation failures retain the ordinary command-buffer exception contract.

Ordinary non-reused prefab spawning uses this same prepare/adopt boundary.
Existing pooled-instance reset and explicit replacement behavior remain intact.
Lua spawning remains synchronous unless its owning engine workflow explicitly
schedules preparation. Input/source invalidation remains the caller's duty;
adoption uses the prepared snapshot and does not reread source files.

The former visual-only payload is now `PreparedTerrainCell`, produced by
`TerrainCellPreparation`. It contains visual batches and prepared interactive
prefab snapshots keyed by stable instance ID. Cell loading prepares both on the
worker. Terrain reconciliation adopts prepared instances for new arrivals, while
existing instances retain their normal state-preserving update path. The source
placement vector is replaced only after successful preparation. Tests verify
that live tags/state survive revision changes and that failed source preparation
preserves the previous visible cell.

Profiler scope and metric writes are now restricted to the frame thread that
owns profiling. Thread-local suppression also covers direct gauges, byte
counters and GPU-scope writes. This fixes direct prefab-cache gauge writes from
private workers racing main-thread frame collection. SceneFlow background jobs
explicitly suppress frame profiling as well. Worker preparation timings remain
in cell telemetry rather than being mixed into the frame timeline.

The same 512 x 512 m CPU fixture (262,144 m2, 144,270 placements, 601 colliders)
was measured in Linux Release with one worker and nine repetitions per mode:

| Procedural arrival phase | Before prefab split | After split, unpaced median | 60 Hz paced median / max |
| --- | ---: | ---: | ---: |
| Scene publication | 33.29 ms | 4.82 ms | 5.26 / 5.91 ms |
| Physics arrival | 1.91 ms | 1.93 ms | 1.91 / 2.11 ms |
| Main arrival combined | 35.34 ms | 6.77 ms | 7.19 / 7.72 ms |

The paced probe updates scene demand and native physics while the worker runs,
with a target tick interval of 16.67 ms. No measured streaming update exceeded
that CPU budget: 0/177 stored ticks and 0/199 procedural ticks. Procedural tick
intervals measured 16.667 ms median and 16.776 ms maximum. This is evidence of
reduced CPU-side loading stalls, not a full rendered frame/input-latency test.
Rendering, GPU uploads, terrain mesh/collision publication and unrelated gameplay
remain excluded. With nine arrival samples, p95 is the observed maximum.

Total procedural request-to-physics-ready latency remains about 357 ms in the
paced probe, including the scheduling wait. Worker generation measured about
54 ms and combined cell preparation about 286 ms. Preparation has moved out of
the frame; it has not become free. The cell still needs prefetching. Runtime
payload accounting reports 29,031,447 bytes per cell for either storage mode,
excluding the overhead and shared resources documented above.

```sh
cmake --build --preset linux-release --target demi-terrain-publication-benchmark
./build/linux-release/demi-terrain-publication-benchmark 512 9
./build/linux-release/demi-terrain-publication-benchmark 512 9 paced
```

Nine focused Release suites pass: prepared prefabs, prefab template cache,
scene/prefab runtime, runtime lifetime failures, renderer, profiler, vegetation
world/residency and ordinary scatter. Coverage includes worker preparation,
project/ID collisions, pending-command preservation, scene ownership, immutable
snapshots, release/revisit, pooling, live-state preservation and profiler writes
from background threads.

Async status is deliberately separate from performance qualification:
vegetation cells support asynchronous loading/preparation with main-thread
adoption; whole finite scenes can generate terrain asynchronously. Automatic
camera-driven regional terrain tile loading/unloading is not implemented yet.
The Alpine world still requires that coordinator, shared environment authority,
coarse fallback coverage, precision handling and rendered traversal qualification.
