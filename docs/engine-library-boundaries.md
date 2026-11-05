# Engine library boundaries and replacement decisions

Updated: 2026-11-03. Scope is the engine, not a particular example. Examples are
behavior/compatibility probes. Existing file boundaries and private interfaces
are not preservation requirements; game behavior and durable data are.

## Implemented core changes

- UI geometry mutations now invalidate document-owned derived geometry. Rendering,
  hit testing and accessibility snapshots resolve pending work at a shared read
  boundary. Unchanged writes do not relayout. Input cancellation remains immediate.
- Native ray batches select candidates once and use the existing exact shape
  intersection implementation. The Lua batch API is engine-wide; the example
  merely migrated from repeated individual queries to that API.
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
