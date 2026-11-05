# DemiEngine architecture and frame-time audit — 2026-11-03

## Decision

Continue the engine, with a stabilization phase and deliberate replacement of
weak subsystem owners. The evidence does not justify a whole-engine rewrite.
Continuing must not mean treating every symptom with another independent cache.

Three GPT-6.1 Sol agents independently reviewed destruction, frame preparation,
and shared contracts/ownership. Their work was read-only. The root investigation
profiled Dungeon and changed the fog rendering path. Static audit findings below
are not measured performance claims unless a trace is explicitly identified.

A low GPU time is not itself a defect in a small 2D game. CPU scene preparation,
visibility decisions, physics and command submission are legitimate. The defect
was doing pixel rasterization on the CPU and repeating invariant preparation in
hot paths. The objective is predictable frame time, not maximizing GPU usage.

## Measured repair: fog ownership

The prior implementation cast visibility rays and rasterized a multi-megabyte
fog bitmap on movement. In the initial Release runtime trace, expensive fog
updates cost about 15 ms: roughly 9 ms of rays and 6 ms of pixel work. Stationary
frames skipped those operations, explaining the low/high alternating pattern.

The replacement keeps a compact CPU polygon and discovery-cell state. A native
GPU fragment program performs coverage, range fade, memory interpolation and
compositing. Movement uploads polygon data, not a CPU-generated mask. A
call-scoped physics broad phase filters reachable colliders once for the ray fan;
the existing exact ray/shape intersection implementation remains shared. No
world-state cache is retained across simulation steps.

Relevant implementation owners:

- `src/demi/runtime/visibility/FogOfWar2D.*`: compact geometry/discovery state.
- `src/demi/runtime/physics/RadialRaycast2D.h` and `PhysicsQueries2D.cpp`:
  call-scoped candidate filtering and shared exact shape queries.
- `src/demi/runtime/render/bgfx2d/FogCanvasRenderer.*`: GPU resources/publication.
- `src/demi/runtime/render/bgfx2d/shaders/fs_demi_fog.sc`: pixel rendering.
- `scripts/profile_dungeon_movement.py`: isolated stationary/moving/settled probe.
- `scripts/test_fog_rendering.py`: rendered pixel assertions for shadows, wall
  contact, antialiasing, range fade, exploration memory and disabling a blocker.

### Controlled Release editor comparison

Both runs used the same isolated authored project, fixed-step input route,
1680x945 editor backbuffer, Vulkan backend, and fixed X11 display. Both had
3–4 simulation steps per rendered frame under this presentation pacing. No
fixed-step cap or gameplay rate was changed. The route begins and ends at (5,5).
This is a repeatable comparison, not a prediction of every user's FPS.

| Moving phase | CPU bitmap path | GPU fog path |
| --- | ---: | ---: |
| Game CPU median | 28.67 ms | 14.71 ms |
| Game CPU p95 | 35.26 ms | 19.43 ms |
| Fog CPU median | 14.66 ms | 1.17 ms |
| Game GPU median, delayed independent clock | 0.102 ms | 0.212 ms |
| Settled Game CPU median | 8.82 ms | 8.73 ms |

Game CPU is the editor's disjoint `Frame.update + Render.submit` metric.
The remaining p95 still exceeds a 16.67 ms frame budget in this probe. An earlier
post-fog trace had spikes dominated by Lua fixed callbacks, which include native
engine API calls; the trace does not isolate scripting, queries or GC sufficiently
to assign the remaining cost to a language or one subsystem.

Artifacts are under `examples/Dungeon/build/profiling/`:
`editor-controlled-before.{csv,json,log}` and
`editor-controlled-after.{csv,json,log}`. Earlier desktop comparisons with changed
window sizes are retained as diagnostics but rejected for this table.
The original sinusoidal chase probe was also replaced with fixed-step signed
input to avoid feedback-dependent path jitter.

Reproduce the current path with:

```sh
xvfb-run -a -s '-screen 0 1600x1000x24' \
  python3 scripts/profile_dungeon_movement.py \
  --binary build/linux-release/demi-editor --editor \
  --output examples/Dungeon/build/profiling/editor-repeat
```

## Priority 1: HUD preparation needs one owner

This is a source-proven repeated-work problem, with incomplete per-operation
profiling. A captured Dungeon session contained 1,277 HUD nodes.

- `LuaScriptHostServices.cpp:30` rebuilds layout in every `setViewport` call;
  `EmbeddedRuntimeSession.cpp:261` calls it every update.
- `LuaScriptHostHudServices.cpp:93` and `:138` immediately rebuild layout for
  rectangle/position/size writes, including sequences of writes in one frame.
- `UiLayoutEngine.cpp:45` reconstructs hierarchy adjacency for each layout.
- `UiInteractionController.cpp:88` builds presentation for hit testing, while
  `UiCanvasRenderer.cpp:122` independently builds it for rendering.
- `UiCanvasRenderer.cpp:315` lays out visible text during every draw.
  `TextLayoutEngine.cpp:121` measures growing prefixes while wrapping;
  `FontAtlas2D.cpp:240` invokes shaping from measurement. The existing layout
  cache is not a drop-in fix: it does not use the renderer's font callbacks.

**Replacement boundary:** resolved UI state owned by the UI document, with
explicit structural, geometry, text and interaction changes. Accumulate
mutations; resolve once when a consumer needs current data. An explicit layout
query must still see preceding writes. Rendering and hit testing consume the
same resolved hierarchy, order and clipping information. Text preparation owns
shaped lines; drawing consumes those lines. This removes repeated work rather
than building separate caches around each caller.

**Qualification:** instrument layout and shaping counts/times. Static HUD frames
should not repeatedly resolve unchanged layout or reshape unchanged labels.
Resize, font changes, safe areas, focus, dynamic children, text editing and
read-after-write queries must remain correct.

## Priority 2: runtime publication must enforce shared invariants

Static inspection found inconsistent validation entry points:

- `RuntimeObjectModel.cpp:68` validates individual components during entity build.
- `RuntimeObjectModel.cpp:118` publishes entities without a complete hierarchy
  and composition validation step; `WorldCommandBuffer.cpp:119` uses this path.
- Parent mutation separately checks cycles at `RuntimeObjectModel.cpp:278`.
- `WorldQueries.h:54` recursively resolves 2D ancestry without a cycle guard.
- CLI validation rejects combinations such as instances without a renderer at
  `Validation.cpp:422`; equivalent runtime composition checks are not shared.

A self-parented runtime-created Transform2D can therefore bypass the mutation
check. A crash was not deliberately reproduced in this audit.

**Replacement boundary:** component-owned semantic validators plus one staged
entity/hierarchy transaction validator, reused by authored loading, editor
commands and runtime commands. Validate the resulting transaction, not each
intermediate mutation separately. Reject invalid worlds before publication and
make hierarchy resolution fail safely when presented with invalid data.

## Priority 3: replace the destruction transaction/scheduling coordinator

The Blast/Jolt integrations and fracture authoring are worth retaining. The
current coordinator has concrete scaling and correctness problems:

1. `DestructionWorld3D.cpp:308` copies all entities and collider assets for a
   local split. `PhysicsWorld3D.cpp:628` then copies all body/ID/frame maps.
   These are real component snapshots, not reference-only copies.
2. `DestructionWorld3D.cpp:211` can copy the world for fading before discovering
   whether any fragment actually needs fading. A split plus cleanup can copy
   the world more than once.
3. `DestructionWorld3D.cpp:542` restarts a sorted-map traversal each fixed step
   and stops after one family. Continuous requests on an early root can starve
   later roots. One family is a count limit, not a time budget; its work remains
   synchronous. Attachment also processes all new assemblies together.
4. `Destructible3DComponent.cpp:36` accepts large authored body budgets, but
   `PhysicsWorld3D.cpp:620` rejects transactions exceeding 256 source or replacement
   bodies. Large splits/restores can fail inside an otherwise permitted budget.
5. Structural topology commits before later damage bookkeeping, impulse and
   cleanup operations (`DestructionWorld3D.cpp:475`, `:578`). A subsequent failure
   can report failure and leave the public revision unchanged after topology has
   already changed. Existing rollback tests mainly cover pre-commit rejection.
6. `DestructionWorld3D.cpp:852` copies expanding pending impact state on each
   request. Queueing repeated requests can produce quadratic aggregate work.
7. Cosmetic debris copies/sorts the full collection and submits individual
   fragments (`CosmeticDebris3D.cpp:96`, `BgfxRenderer3DDebris.cpp:18`). Per-owner
   budgets do not bound total work across many damaged owners.

**Replacement boundary:** affected-assembly transactions and a fair, explicit
preparation/publication queue. Stage only affected component/native-body records;
preallocate fallible work before publication; define revision/failure outcomes
for cleanup separately where needed. Separate immutable compiled fracture and
collision data from instance damage state. Give cosmetic rendering culling,
instancing and a global budget.

**Qualification:** continuous traffic on multiple roots must not starve one;
adding unrelated entities must not make a small fracture copy the entire world;
post-commit failure injection must leave honest state/revisions; the declared
body limit must match validation and native execution. Measure visible split,
restore and retirement workloads before choosing GPU simulation or threading.
CPU fracture topology and CPU physics are not inherently wrong.

## Other owners needing work

### Visible render preparation

`TilemapCanvasRenderer.cpp:42` traverses all cells and prepares overrides,
animation/texture selection before viewport rejection at line 101. Dungeon's
60x46 map means 2,760 positions per layer. `IsoCanvasRenderer.cpp:55` also submits
all cells/grid edges without view rejection. Sprite gathering/sorting precedes
visibility selection (`SpriteCanvasRenderer.cpp:39`).

First compute visible ranges and prepare visible data. Use changed chunks where
map scale justifies them. Keep the shared renderer/backend; do not rewrite bgfx
because callers prepare invisible content.

### Physics and hierarchy synchronization

`EntityComponentQuery.h:15` creates and sorts a new index vector per query.
`Physics2D.cpp:100`, `:371`, `:550` repeatedly issues queries and reconstructs
membership maps; `:272` and `:666` destroy/recreate joints each step. The observed
Box2D solve itself was small compared with synchronization. This does not make
physics the main current Dungeon spike.

Use explicit membership/property changes, stable body/joint records and one
shared hierarchy owner. Transform2D currently lacks the tracked-mutation
contract needed for correct revision-based derived data. Fix that contract before
adding caches. Ordinary spatial queries and kinematic movement also retain
whole-world scans outside the new call-scoped ray fan.

### Session, scripting and editor ownership

`LuaScriptHost.h:717`/`:787` owns several runtime services and scene/resource
lifetimes; `LuaScriptHost.cpp:154` implements audio/video autoplay. Splitting the
class into more files does not change that ownership. Move engine service
lifetimes to runtime-session composition and inject narrow adapters into Lua.

`EditorWorkspace.cpp:662` manually selects subsystem-specific preview rebuild,
patch and rollback behavior; `:1687` manually restores native/configuration
terrain state. `EditorScenePreview.cpp:37` partly infers generated provenance
from ID prefixes. Use a shared preview transaction/change-impact boundary and
explicit generated-object ownership, while preserving the separate authored
and live-preview worlds.

### Contracts and feature qualification

Complex animation data has separate parser/schema/runtime checks. HUD parser,
Inspector and schema enumerate properties independently and have drifted.
Unify the property/validation contract, not necessarily the storage models.
Retained game UI and editor ImGui serve different purposes; 2D/3D physics and
transforms also have legitimate separate requirements. `Entity.get_config`
intentionally reports authored configuration, not live state; that is not a bug.

The capability page and newer material documentation disagree on delivered
PBR/HDR capabilities. Reconcile implemented, tested and qualified status. A
registered API name or a narrow passing test is not proof of a useful complete
feature.

## What to retain, and when to reconsider

Retain stable IDs and versioned authoring, the asset/cook/package pipeline,
SDL/bgfx ownership boundaries, Box2D/Jolt/Blast integrations, component reflection
and native field bindings, shared parsers, conflict-aware editor document storage,
and the existing regression corpus.

Pause feature expansion while repairing the UI update boundary, shared runtime
invariants and destruction coordinator. Qualify two representative vertical
slices: Dungeon movement/UI and a visible destruction stress scene. Set budgets
on a named reference build/machine and inspect worst frames as well as p95.
Lua callback scopes include native API work; high Lua-scope time is not by itself
a reason to replace Lua.

Reconsider a larger rewrite only if these replacements repeatedly require
breaking otherwise useful public contracts or cannot be isolated from the rest
of the engine. This audit did not establish that. It also did not run failure
injection or destruction performance benchmarks; those remain the next evidence
needed before implementing the destruction redesign.
