# Engine library boundaries and replacement decisions

Updated: 2026-11-05. Scope is the engine, not a particular example. Examples are
behavior/compatibility probes. Existing file boundaries and private interfaces
are not preservation requirements; game behavior and durable data are.

## Implemented core changes

- UI geometry mutations now invalidate document-owned derived geometry. Rendering,
  hit testing and accessibility snapshots resolve pending work at a shared read
  boundary. Unchanged writes do not relayout. Input cancellation remains immediate.
- Native 2D ray batches select candidates once and use Box2D shape queries.
  Simulation and queries share fixture construction; custom ray/overlap math
  and bounding-box substitutes for capsules/polygons were removed. The Lua
  batch API is unchanged.
- Font registration now owns immutable HarfBuzz font/face resources. Coverage
  queries use those library objects. A shaping operation creates a sized sub-font
  at most once per used face and reuses its buffer between runs.
- Cluster selection uses indexed ranges in the decoded codepoint stream instead
  of rescanning the whole stream and allocating a new vector per cluster.
- Removed the custom codepoint coverage-cache policy and the unused synthetic
  `TextLayoutCache`, including its key construction, LRU and unused request
  revision field. The latter was not used by production rendering and lacked
  its actual font measurement/shaping callbacks.

The HarfBuzz change uses the existing pinned dependency, not a new engine-owned
font cache. [HarfBuzz font lifecycle documentation](https://harfbuzz.github.io/harfbuzz-hb-font.html)
provides immutable fonts, sub-fonts and native coverage queries.

A Release engine-only paragraph-layout benchmark (`demi-text-pipeline-benchmark`)
measured 15.54 ms median / 16.81 ms p95 before and approximately 1.50 ms / 1.53 ms
after the font-resource change. The line-width, glyph-ID, advance and offset
fingerprint was identical: `10873205588425785091`. This is not a whole-frame FPS
claim or proof that every text case is optimized. Greedy line breaking is still
engine-owned, and repeated unchanged text preparation remains a separate issue.

Core qualification for this pass: eight focused native UI/font/query tests,
Orbit UI's live controls/runtime-factory E2E test, a Vulkan UI-showcase startup
smoke, and the existing eight Dungeon behavior scenarios pass. No test assertions
were relaxed to accept changed layout or glyph output; renderer fixture inputs
were migrated from writing derived rectangles to canonical layout inputs.

## Native queries and CLI consolidation (2026-11-05)

This pass replaces private implementations without migrating authored games.

- `Box2DShapes2D` owns the fixture conversion used by simulation and operation-
  scoped query snapshots. Box2D owns ray intersections and shape distance tests.
  Parent transforms and immediate authored changes remain visible to queries.
  Radial batches retain only nearby native shapes. Candidate discovery still
  scans entities; this does not claim a persistent native broadphase migration.
  Overlaps include Box2D's polygon margin and retain existing ground-probe slop.
- `SpatialQuery3D` uses the existing Jolt shape factory and native transformed-
  shape queries before simulation, rather than separate approximations and
  sampled sweeps. Allocating a physics world or stepping with zero delta does
  not hide authored colliders. After body synchronization, the existing native
  broadphase remains in use, with its fixed-step synchronization semantics.
  Candidate-pose movement tests remain isolated from live bodies. Unsupported
  static mesh pairs return false; convex compounds remain supported.
- CLI11 2.6.2 is pinned by commit and linked only into desktop tools/entrypoints.
  Command declarations own parsing, required fields, numeric conversion and
  help; the old `hasArg`, `valueAfter`, and `projectFileFromArgs` paths are gone.
  A small argv/stream adapter preserves focused command entry points and maps
  usage failures to status 2. It does not retain a second parser. Runtime,
  editor and Android device forwarding use the same argument validation path.

Invalid options and malformed numbers that some old commands silently ignored
now fail before execution. Help exits successfully. JSON authoring, Lua query
signatures, scene identities and saved data formats do not change.

### Qualification and maintenance accounting

Against baseline `53cd0242`, this pass removes **722 net production-source
lines**, counting new adapter files in `src/`. Tests and documentation are
reported separately, not subtracted to inflate that number. CLI/runtime/editor
argument handling accounts for 294 fewer lines. Physics accounts for 443 fewer
lines; a 15-line render-boundary normalization fix accounts for the remainder.

The native shapes exposed coincident corner samples in the fog render polygon.
Adjacent points within the existing shader's degenerate-edge threshold are now
coalesced before upload, retaining distinct near/far silhouette jumps. This
restores fractional pixel coverage on exact diagonal edges. The GPU regression
probe now holds each fixture in its own process until capture: at 5120x2806,
screenshot latency could otherwise capture the next timed phase. The checks
still require wall occlusion, narrow edge coverage, discovery updates without
recasting, open doors and unobstructed actor-layer geometry.

Release qualification: 22 focused/workflow CTests, all eight Dungeon desktop
E2E scenarios, and all five maximized GPU fog fixtures pass. Dungeon (71 source
files), minimal 3D (9) and the minimal Android 2D project (12, Android target)
validate without diagnostics. Android validation is not a device execution test.
The CLI, standalone runtime, editor and server binaries were rebuilt.

A matched 14-second Release standalone dev probe used the same 3840x2022
viewport and engine-driven six-second movement phase, with the existing 60 FPS
cap unchanged:

| Moving-phase scope | Before median / p99 | After median / p99 |
| --- | ---: | ---: |
| Visibility ray queries | 0.703 / 1.173 ms | 0.302 / 0.510 ms |
| Game CPU | 4.588 / 7.745 ms | 4.655 / 7.426 ms |
| Frame interval | 16.667 / 16.769 ms | 16.666 / 16.744 ms |

This supports reduced query cost, not a whole-frame speedup or 165 Hz
qualification. Evidence is under
`build/linux-release/qualification/native-query-migration/`: use `before.*`
and `after-matched.*` for comparison, and `fog-static/` for the final rendered
checks. Intermediate captures remain diagnostic evidence only; `after-final.*`
used a different maximized viewport and is excluded from the comparison.

## Follow-up shared-rule consolidation (2026-11-05)

The four follow-up findings are now implemented without new dependencies or
changes to authored formats and Lua APIs.

- **Camera projection:** `Camera3DMath` owns a backend-neutral resolved projection
  with explicit-frame and authored-transform constructors, screen/ray conversion,
  and pixel scale. World text and terrain draw selection share it. Both now use
  the full orthographic visible height, fixing text displacement/scale and the
  factor-of-two underestimate of terrain screen error. Tests use known screen
  positions and pixel budgets rather than repeating the implementation formula.
  Inverted up axes, parented labels, explicit render poses and perspective views
  remain covered. Degenerate view directions do not define a projection.
- **Material interpretation:** the existing `MaterialSurface` owner provides
  normalized mesh-scalar precedence/default/clamping and surface-mode naming.
  Inspector fallback and renderer resolution share that policy; MaterialLibrary
  reuses typed raw material values and base color. Explicit fields still override
  material properties, then inherited values/defaults. Clearing fields preserves
  inheritance. Custom-shader parameter data is not clamped during loading.
  GPU draw state/fade and Inspector provenance remain with their original owners.
- **Destruction:** normal and transactional replacement bodies share rigidbody
  settings conversion. Fade eligibility is checked before staging entity/asset
  copies, retaining transactional publication and rollback. A test-only observer
  on 256 unrelated entities measured **512 copy constructions before, zero after**
  partial damage with no detached group eligible for fading. This measures avoided
  copying, not a whole-frame benchmark. Real split/fade/motion/checkpoint/rollback
  scenarios continue to pass.
- **Data/package utilities:** bounded utf8proc decoding replaces handwritten UTF-8
  validation, preserving original bytes and embedded NUL handling. Archive byte
  hashing uses the existing PackageHash implementation. Hash encoding, archive
  format, asset FNV hashes, resource limits and extraction safeguards are unchanged.

Release qualification: nine focused CTest suites pass (camera alignment,
Inspector, material asset/library, renderer, physics, destruction, data and
packages). The destruction lab's one E2E scenario and animation example's two
E2E scenarios pass through engine Test input. Minimal voxel (9 source files),
terrain (206), destruction (5) and animation (7) validate without diagnostics.
A maximized-window Vulkan probe at 3840x2022 visually verifies world labels
against geometry, with pixel assertions at the analytically expected marker
positions. Evidence: `build/linux-release/qualification/shared-rules-followup/`.

This pass removes another **39 net production-source lines** relative to the
previous 722-line reduction (761 total against `53cd0242`, including new adapter
files, excluding tests/docs). Material centralization is approximately line-count
neutral; readable destruction control flow is longer than the previous compressed
code. The gains are shared policy, corrected projection and eliminated no-op work,
not an attempt to minimize lines at the expense of clarity.

The larger archive-format, generic-reflection and delta-world transaction
rewrites remain deferred: they were not needed for these bounded changes.

## Preferred replacement: Blast authoring

The pinned SDK already contains fracture authoring, mesh cleaning, convex
collision generation hooks and bond generation. Its
[authoring API](https://docs.omniverse.nvidia.com/kit/docs/blast-sdk/latest/_build/docs/blast-sdk/latest/file_extensions_authoring_NvBlastExtAuthoring.h.html)
overlaps with custom clipping, bisection, cap construction and contact-based bond
creation in `ConvexFracture.cpp` and `FracturePrefab.cpp`.

The intended replacement removes the geometric implementation, retaining a thin
adapter for source loading, recipes, IDs, material mapping, diagnostics and
transactional asset publication. It must not introduce PhysX merely to obtain
CPU authoring. The authoring extension needs a separately audited build target
and a collision-mesh builder adapter. Determinism, interiors, mass/volume,
unsupported geometry and checkpoint compatibility must be qualified before
switching the generator. This integration is evaluated, not implemented yet.

## Preferred representation: native Jolt shapes

[Jolt](https://github.com/jrouwe/JoltPhysics/blob/master/Docs/Architecture.md)
already provides reference-counted collision shapes and compounds. Fracture
instances should reference prepared native leaves rather than duplicate point
arrays and reconstruct their convex hulls for every split. Authored assets stay
backend-neutral; native prepared shape ownership belongs to physics.

This does not replace Demi's stable part mapping, momentum transfer, fair
scheduling or atomic entity/body publication. Those are engine integration
responsibilities, and still require the coordinator redesign identified in the
[architecture audit](engine-architecture-audit-2026-11-03.md).

## RmlUi: migration reverted

The user rejected the migration on 2026-11-04 because its integration and visual
authoring costs did not demonstrate the intended maintenance benefit. The native
RML/RCSS integration, dependency, authoring prototype and example conversion were
removed. Existing JSON HUD authoring, the visual editor, event annotations and
localization are restored. Independent engine/gameplay fixes are retained.
The pre-rollback working tree was backed up before restoring selected paths;
this was not a repository-wide reset.

## Library adoption rule

A replacement proposal must name the engine code it deletes, the library-owned
behavior replacing it, the necessary adapters, and the migration/qualification
work. Evaluate startup, steady-state and mutation costs. Do not keep competing
shipping implementations as a permanent compatibility layer. Do not remove
validation, Unicode handling or observable behavior simply to reduce a complexity
score.

Cyclomatic complexity is one review signal. It does not reveal the cost of a
whole-world copy or repeated library construction. The cited
[GPT-4/Python comparison](https://arxiv.org/html/2501.16857v1) found higher average
complexity in its particular 72-task, one-human comparison; it does not establish
a universal result for modern models or game engines. The second cited
[ICSE paper](https://www.se.cs.uni-saarland.de/publications/docs/APM%2B26.pdf)
studies alignment in code comprehension, not generation complexity. Repository
measurements and independent behavior tests remain the acceptance evidence.
