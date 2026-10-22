# 3D showcase and authoring plan

Planning record, 2026-10-21. The requested Flax screenshots are feature references,
not evidence that DemiEngine implements those features. This plan covers the
requested workflows and separates verified code from proposed work. It does not
promise full Flax parity or change the engine's Linux-first, experimental
lightweight-3D support status.

Silvermere, in `examples/terrain_3d`, is the integration scene for a convincing
terrain environment and camera tour. Its current construction and qualification
belong in [the showcase record](docs/silvermere-showcase.md). Feature completion
requires reusable engine/package boundaries and authoring tools, not a script
that makes one demonstration appear to have a missing editor or renderer feature.

## Verified baseline

**Implemented** means the stated bounded behavior exists. **Partial** means
usable foundations exist but the requested workflow is incomplete. **Missing**
means no implementation of the requested capability was found in the inspected
runtime/editor/importer paths. None of these labels substitutes for target
qualification, visual review, or performance evidence.

| Requested workflow | Current status and exact boundary | Evidence |
| --- | --- | --- |
| Terrain editor | **Implemented, bounded.** Shared terrain assets, procedural graph/settings, sculpt/flatten/smooth, biome/protection/exclusion strokes, Undo/Redo, generated chunks, material layers, water and render LOD. Large-world streaming and coordinated collision residency remain incomplete. | [Terrain authoring](tools/package-store/templates/docs/content/terrain.html.twig), [terrain Inspector](src/editor/EditorTerrainInspector.cpp), [terrain LOD](docs/terrain-lod-qualification.md) |
| Foliage editor | **Partial.** Named deterministic placement rules, physical-density scatter, biome/slope/water constraints, exclusion painting, batched groundcover, selective collision, wind, authored model LOD, and conversion to scene objects exist. A dedicated foliage painting/selection workflow with brush species, density, erase and live budget feedback is not delivered by the existing terrain placement list. | [Terrain guide](tools/package-store/templates/docs/content/terrain.html.twig), [scatter Inspector](src/editor/EditorTerrainInspector.cpp), [Milestone 6 evidence](docs/terrain-milestone6-qualification.md) |
| Async content streaming | **Partial.** Async scene preparation, loading progress, explicit asset loading/groups and residency mechanisms exist. Some publication/GPU/physics work remains on the frame thread. Terrain streaming has a reference relevance evaluator; that alone does not load/evict complete world cells or coordinate render, collision, scripts and saves. | [SceneFlow](src/demi/runtime/scene/SceneFlow.h), [asset API](scripts/stubs/demi/assets.lua), [terrain streaming policy](src/demi/runtime/terrain/TerrainStreaming.h) |
| Scene animation timeline | **Missing as an editor workflow.** `demi.camera.paths` supplies reusable authored position/aim interpolation with pause/seek/loop for the showcase. This is not a native multi-track scene-animation asset or visual timeline. Existing `demi.cutscene` play/pause state does not supply track evaluation. | [Camera-path package](packages/sources/demi.camera.paths/README.md), [cutscene service implementation](src/demi/runtime/scripting/LuaScriptHostMediaNetworkServices.cpp) |
| Visual material editor | **Partial.** Integrated Material documents expose maps, parameters, render state, asset pickers, Undo/Redo and Save. UV/triplanar mapping, normal/roughness/metallic/AO/detail maps and terrain layering are implemented. A visual shader/material node compiler and node-preview workflow are missing. | [Material contract](tools/package-store/templates/docs/content/materials.html.twig), [material controls](src/editor/EditorMaterialPropertiesControl.cpp) |
| Automatic model LOD generation | **Missing.** Runtime selection consumes separately authored medium/low model references; animation has clip-compatible model LOD and pose-rate budgets. Terrain generates its own heightfield LOD. None of these automatically simplifies an arbitrary imported model into new mesh LODs. | [MeshRenderer fields](src/demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h), [model import profile](src/demi/assets/ModelImportProfile.h), [runtime model selection](src/demi/runtime/render/BgfxRenderer3DModelLod.cpp) |
| Volumetric lighting | **Missing.** Environment distance fog and existing shadows/lighting do not compute participating-media light scattering, shadowed fog volumes, or volumetric shafts. | [Environment contract](src/demi/runtime/scene/components/3dcomponents/Environment3DComponent.h), [lighting guide](tools/package-store/templates/docs/content/lights-and-camera-3d.html.twig) |
| Cinematic postprocess | **Partial.** Exposure, contrast, saturation, tint, vignette, bloom controls and fades exist. Do not infer a complete HDR pipeline, cinematic depth of field, motion blur, temporal effects or an artistic color-grading tool from those fields. | [PostProcessStack](src/demi/runtime/scene/components/3dcomponents/PostProcessStackComponent.h), [lighting guide](tools/package-store/templates/docs/content/lights-and-camera-3d.html.twig) |
| CPU particles | **Implemented, bounded.** Native deterministic particle simulation, emitter shapes, seeded variation, world/local space, bounded counts and billboard presentation exist. This is a parameterized emitter system rather than a full effects graph. | [3D simulation owner](src/demi/runtime/render/ParticleSimulation3D.h), [emitter metadata](src/demi/runtime/scene/components/3dcomponents/ParticleEmitter3DComponent.h) |
| GPU particles and visual particle editor | **Missing for the requested workflow.** Rendering particle billboards on the GPU does not mean particle simulation runs on the GPU. No compute simulation plus visual emitter/module graph was found. Existing generic Inspector controls remain useful foundations. | [CPU simulation](src/demi/runtime/render/ParticleSimulation3D.h), [billboard renderer](src/demi/runtime/render/bgfx3d/ParticleBillboardRenderer3D.h) |
| Nested prefabs | **Implemented, bounded.** Nested instances, stable expanded IDs, overrides, origin tracking and shared composition exist. Additional authoring polish must preserve that model rather than introduce another nesting format. | [Prefab guide](tools/package-store/templates/docs/content/prefabs-and-instances.html.twig), [shared resolver](src/demi/runtime/scene/composition/PrefabResolver.cpp), [origin tests](tests/prefab_origin_index_tests.cpp) |
| GPU lightmap baking | **Missing.** Material AO textures and real-time directional shadows are not baked indirect lighting. No lightmap UV/atlas/bake/probe pipeline or GPU baker was found. | [Material map meanings](tools/package-store/templates/docs/content/materials.html.twig), [lighting guide](tools/package-store/templates/docs/content/lights-and-camera-3d.html.twig) |
| Animation graph and IK | **Partial.** State machines, transitions, parameters, layers/masks, root motion, imported skeletal clips, GPU skinning, visual pose budgets and two-bone IK/bone-segment APIs exist. The state-machine editor is a form-based panel; an arbitrary visual pose/blend graph and production full-body IK toolset are not delivered. | [Animation panel](src/editor/EditorAnimationMachinePanel.cpp), [animation API](scripts/stubs/demi/animation.lua), [animation runtime](src/demi/runtime/animation/AnimationRuntime.h) |
| Profiler | **Implemented, bounded.** CPU scope/frame distributions, CSV traces, process CPU/RAM, renderer counters and available delayed GPU view timings exist. GPU samples are not synchronous CPU costs. Full GPU allocation attribution, shader profiling and every external driver tool are outside this claim. | [Profiler guide](tools/package-store/templates/docs/content/replay-and-profiler.html.twig), [runtime profiler](src/demi/runtime/profiling/RuntimeProfiler.cpp) |
| Navigation/pathfinding/navmesh | **Partial.** Native 2D grid navigation/path queries exist, with reusable terrain-ground gameplay helpers. A baked/tiled 3D navmesh, off-mesh links, agent-radius builds and complete crowd navigation are missing. Physics casts and ground sampling do not replace them. | [Grid API](scripts/stubs/demi/navigation.lua), [native grid](src/demi/runtime/navigation/NavigationGrid2D.h), [ground helper](packages/sources/demi.navigation.ground/README.md) |
| C++/C# hot reload versus Lua | **Lua implemented; native/managed gameplay reload missing.** C++20 builds the engine and editor; rebuilt native code requires a process restart. No C# gameplay runtime or safe C++ gameplay DLL-reload boundary is provided. Lua source hot reload keeps the previous valid instance after a failed reload; instance fields reset on successful reload, so durable game state belongs elsewhere. | [Lua reload contract](tools/package-store/templates/docs/content/scripting.html.twig), [lifecycle](tools/package-store/templates/docs/content/script-lifecycle.html.twig), [native build rules](AGENTS.md) |

This inventory follows current implementations where older overview pages lag
behind newer maintained feature pages; for example, a blanket statement that all
PBR material support is absent conflicts with the current Material contract.
Resolve such documentation inconsistencies before publishing new capability
claims. The source links above identify what was inspected, not certification of
all combinations of features.

## Shared implementation rules

Keep versioned source assets, stable IDs, deterministic cooking, target
validation, transactional publication and reversible document commands. New
components/fields require the full parser, schema, metadata, editor, Lua stub,
example and test contract. Do not hide renderer handles in scripts or create
editor-only runtime models.

Each phase starts with a small repeatable acceptance scene, then integrates into
Silvermere. Existing authored work must remain valid or receive an explicit
alpha migration. Generated LODs, navmeshes, shaders and bakes belong in cooked or
generated outputs; their stable identity belongs to versioned source settings.

The acceptance-scene IDs below are proposed fixtures, not existing features.
Linux Release with Vulkan is the initial visual/performance target. Record
hardware, resolution, camera route, density, frame-time distributions, CPU/GPU
subsystems and memory. Set budgets from Phase 0 measurements before committing
numbers. Debug timings cannot define hardware limits. Android receives separate
capability, compilation and physical-device qualification for supported paths;
unsupported features retain actionable diagnostics.

## Phase 0 — qualify the environment and expose honest gaps

**Dependencies:** existing engine and current package sources.

- Finish Silvermere composition with deliberate terrain silhouettes, water,
  distinct foliage scales, architecture and paths; use licensed source assets
  with provenance and materials that survive close views.
- Qualify the complete camera route, not only the opening frame: no terrain
  penetration, abrupt orientation flips, clipped landmarks or unexplained stalls.
- Verify pause/resume/restart/chapter navigation and a natural complete loop.
  Keep camera motion as authored package data until native timeline work exists.
- Capture a matched source/cooked visual set, real entity/placement/triangle
  counts, warm/cold load behavior and repeatable profiler traces. Keep visible
  changes and storage/performance optimizations separately attributable.

**Acceptance:** `scene://terrain_3d/main` plus its loading scene, stable-HUD-ID
E2E, a complete visual tour and a signed-off baseline record. The showcase may be
visually complete while later engine features remain planned.

## Phase 1 — close daily content-authoring gaps

**Dependencies:** Phase 0 fixtures; shared importer and document commands.

1. **Automatic static model LODs:** add importer-owned simplification settings,
   geometric error/count targets, material-boundary and UV/normal preservation,
   deterministic dependency hashes, cancellation and transactional reimport.
   Feed the existing runtime LOD selection rather than create another selector.
   Start with static meshes; skinned LOD needs separate skin-weight, skeleton,
   clip and deformation qualification. Keep collision independently authored.
2. **Foliage tools:** add species selection, density/scale/rotation controls,
   paint/erase strokes and budget feedback over existing placement/exclusion
   ownership. Undo restores stable placements; regeneration and conversion to
   scene objects retain suppression and collision semantics.
3. **Scene-animation assets/timeline:** add versioned transform/camera/property
   tracks with stable targets, keyframes, interpolation, duration, loop, scrub,
   pause, seek and deterministic evaluation. Reuse reflected field mutation and
   event-driven transforms. Define editor preview isolation, authored versus
   runtime overrides, event-track seeking and save behavior explicitly. Migrate
   camera paths only when the native workflow covers their contract.
4. **Nested-prefab polish:** expose deep selection/override provenance, apply/
   revert and missing-reference diagnostics through existing composition and
   document commands; preserve small source diffs.

**Acceptance:** proposed `scene://qualification/lod_gallery`,
`scene://qualification/foliage_authoring` and
`scene://qualification/scene_animation`. Verify silhouette/material/normal
quality at transition distances, deterministic reimport, stroke Undo/Redo,
no unrelated scene changes from scrubbing, nested override round trips, and
source/cooked parity. Silvermere then consumes the same tools and assets.

## Phase 2 — world traversal and navigation

**Dependencies:** measured Phase 0 limits; stable asset/scene lifetimes; Phase 1
LODs for dense content. Navigation can begin on a fixed scene before streaming.

- Build a prepared streaming coordinator over the terrain relevance policy:
  bounded worker jobs, shared asset leases, cancellation, GPU upload budgets,
  collision/gameplay anchors, hysteresis, eviction and late completion checks.
  Avoid duplicating async scene/asset mechanisms. Saved world state must survive
  unloading; unpublished work cannot leak rows or assets into active queries.
- Bake/query a versioned 3D navmesh from shared collision/terrain geometry with
  agent dimensions, slope/step limits, areas and stable links. Add editor bake
  bounds, debug overlay and actionable diagnostics before crowd features.
- Define tile ownership and runtime link/obstacle behavior before integrating
  navigation with streamed cell arrival/eviction. Preserve authoritative game
  rules and deterministic bounded queries.

**Acceptance:** proposed `scene://qualification/streaming_valley` and
`scene://qualification/navigation_village`: repeated forward/back traversal,
teleport/cancel/reload, bounded residency, no missing gameplay collision,
saved-state retention, reachable/unreachable routes, stairs/doors, multiple agent
sizes, blocked links, disconnecting tiles and cooked results. Report cold and
warm stalls with subsystem evidence, not only FPS.

## Phase 3 — material and cinematic rendering workflow

**Dependencies:** Phase 0 visual baseline and render-target/lifecycle contracts.
A small material node subset does not require finishing every cinematic effect.

- Extend integrated Material documents with a typed visual graph, validated
  links, texture/sampler conventions, deterministic compilation, dependency
  tracking, actionable shader errors and last-valid preview recovery. Keep
  existing Material identity and Inspector/source serialization authoritative.
- Qualify linear/HDR color and render-target formats before advanced grading or
  effects that depend on them. Specify actual bloom/exposure behavior rather
  than infer an HDR pipeline from control names.
- Add cinematic controls in bounded steps: grading/preview first, then depth of
  field or motion blur only with their depth/velocity/history dependencies and
  lifecycle tests. Keep water capture, MSAA, embedded Game targets and Android
  surface recreation correct.
- Add volumetric lighting as an explicit participating-media pass with measured
  resolution/history/shadow costs. Distance fog remains a separate existing
  feature. Test moving camera/lights and translucent-water composition.

**Acceptance:** proposed `scene://qualification/material_gallery` and
`scene://qualification/cinematic_lighting`: known color/normal/scalar fixtures,
close foliage cutout/shadow views, shader-error recovery, source/cooked matching,
pause/scrub/resize/Play lifecycle, bright/dark exposure sequences, moving
volumetric occluders and quality/performance comparisons at fixed cameras.

## Phase 4 — effects and character authoring

**Dependencies:** reflected animation mutation, Phase 3 material/render contracts
and measured runtime budgets. CPU simulation remains available independently.

- Add a visual particle authoring document over the existing CPU emitter first:
  preview, seed, parameter curves, bounds, duration, restart and undoable edits.
- Evaluate GPU simulation only for effects requiring it. Define capabilities,
  determinism/replay limits, resource budgets, readback avoidance, sorting,
  collision scope and CPU equivalence before exposing GPU-only modules.
- Add a typed animation pose graph over existing sampling/blending: clip,
  transition/blend and mask nodes with diagnostics and saved previews. Preserve
  root motion, gameplay events, visual pose budgets and physics independence.
- Improve IK authoring from the existing two-bone primitives: target/pole debug
  views, joint-space conventions, constraints, blend weights and predictable
  evaluation order. Add more solvers only for demonstrated rigs.

**Acceptance:** proposed `scene://qualification/effects_lab` and
`scene://qualification/character_lab`: repeatable seeded CPU effects, invalid
module recovery, GPU capability rejection/lifecycle/resource cleanup, state/
blend transitions, feet on uneven terrain, unreachable IK targets, root-motion
collision and events independent of visual update rate.

## Phase 5 — baked lighting; explicit language boundary decision

**Dependencies:** stable geometry/material import, UV validation, Phase 3 color
pipeline and target/resource budgets. This phase is not required to call the
initial terrain showcase complete.

- Scope baked lighting before selecting a GPU baker: source settings, lightmap
  UVs/charts, atlas padding, texel density, bounce/material model, cancellation,
  progress, deterministic cache keys and transactional bake publication.
  Moving actors need a separately specified probe/indirect-light policy.
- Qualify runtime lightmap sampling, seams, overlap diagnostics, mixed static/
  dynamic shadows and package contents before claiming GPU lightmap support.
- Keep Lua as the supported gameplay iteration boundary. Treat C++ gameplay hot
  reload or C# as a separate architecture/product decision, not a checkbox:
  ABI/ownership, world serialization, plugin unload safety, errors, debugger,
  dependencies, packaging and platform support all require explicit contracts.
  Native engine rebuild/restart remains valid; no language runtime is added
  solely to resemble a reference screenshot.

**Acceptance:** proposed `scene://qualification/baked_courtyard`: reference
lighting, UV-overlap failure, source change invalidation, cancelled/failed bake
retaining the last artifact, moving-object policy and cooked-source parity.
Any new language requires its own small gameplay/reload qualification scene
and crash/state-preservation tests before a general showcase integration.

## Library evaluation boundaries

Prefer established libraries for the algorithms they own; retain engine-owned
identity, authoring, validation, cooking, ownership and presentation contracts.
Candidate status does not imply a dependency has been adopted.

- [meshoptimizer](https://github.com/zeux/meshoptimizer) is a focused candidate
  for importer mesh simplification/optimization. Evaluate attribute/material
  preservation, requested versus achieved error and index-count targets, source
  determinism and cooking cost on the LOD gallery. It is not the runtime LOD
  policy, skinned-rig authoring or terrain streaming system.
- [Recast/Detour](https://github.com/recastnavigation/recastnavigation) is a
  candidate for 3D navmesh generation and path queries. Evaluate agent settings,
  tile invalidation, links, serialized data and terrain/collision integration;
  keep the working 2D grid and game authority boundaries.
- [ozz-animation](https://guillaumeblanc.github.io/ozz-animation/documentation/features/)
  is an optional evaluation for sampling/blending/IK workloads if measured
  limits justify it. It does not supply DemiEngine's graph editor, stable clip/
  asset contracts, gameplay events or physics ownership. Do not replace working
  skinning/runtime paths without comparative correctness and profiling evidence.

For each evaluation record pinned version/license, Linux build, relevant Android
compile/device implications, equivalent output fixtures and integration cost.
Do not introduce multiple implementations of the same responsibility to avoid
making a decision; keep rejected-library evidence and a single selected owner.

## Completion and reporting

A milestone closes only when its maintained website documentation, source
contract, editor, importer/cook, Lua metadata where public, focused tests and
acceptance scenes agree. Record exact failing gates and platform limitations.
Keep new measurements in qualification records, with matching source revisions
and reproducible commands; this plan itself supplies no unmeasured performance
promises. Link new public pages through `tools/package-store/src/Docs.php` when
implementation is ready. This root plan is an internal roadmap, not an
announcement that planned features are available.
