# Library reduction and fallback audit

Read-only source review, 2026-10-21, based on `f1296f1f` plus the live working
tree. The concurrent EnTT migration is intentionally excluded from architectural
judgments. No builds, device runs or performance measurements were performed.
Findings are opportunities, not claims of measured savings or qualified ports.

## Ranked replacement opportunities

1. **Reuse the existing cgltf integration for all glTF decoding.**
   `src/demi/assets/GltfGeometry.cpp:18` owns another accessor/buffer decoder,
   node transforms and hierarchy traversal (379 lines total).
   `src/demi/assets/ColliderAssetGenerator.cpp:24` independently implements
   matrices, hierarchy and accessor-bound traversal through approximately line
   244. Meanwhile `src/demi/assets/GltfSkinnedModel.cpp:1` already embeds cgltf,
   and `cmake/DemiCore.cmake:180` exposes bgfx's bundled cgltf headers. Make one
   private adapter own parsing, buffer lifetime, validation, transforms and
   accessor reads; retain Demi import normalization, collider recommendations,
   diagnostics and stable-ID contracts. Delete those duplicate mechanisms after
   corpus comparison; do not claim either entire file disappears. Existing
   dependency, MIT, portable C99; Linux/Android build and malformed-asset tests
   remain required. Medium effort. Avoid adding another implementation macro or
   assuming bundled revision equals current upstream.
   Sources: [cgltf API and license](https://github.com/jkuhlmann/cgltf),
   [API implementation](https://github.com/jkuhlmann/cgltf/blob/master/cgltf.h).

2. **Replace two partial JSON Schema interpreters with one library adapter.**
   `src/demi/assets/DataAsset.cpp:65` and
   `src/demi/runtime/network/NetworkMessageGateway.cpp:90` separately interpret
   type/enum/range/required/properties/items, with different supported keywords.
   Replace their generic validation portions, retaining Demi URI resolution,
   diagnostics, immutable data conversion and network authority/resource limits.
   New candidate: jsoncons JSON Schema (Boost Software License, header-only,
   supports draft 2020-12). This matches the dialect declared by
   `schemas/project.schema.json:2`; pboettch's otherwise convenient nlohmann
   adapter supports draft 7 and is not a drop-in choice for all repository
   schemas. Jsoncons adds a second JSON representation, so verify adapter size
   and actual deleted code before adoption. Medium/high effort; Linux/Android
   compilation, diagnostic compatibility, bounded evaluation and an offline
   reference resolver are qualification gates. Never fetch arbitrary remote
   schema references from a network message. Keep custom `reference` checks.
   Sources: [jsoncons supported dialects/license](https://github.com/danielaparker/jsoncons),
   [draft-7 alternative](https://github.com/pboettch/json-schema-validator).
   Concrete defect discovered: `DataAsset.cpp:156` tests only whether the schema
   contains `items` before using `array->size()`, even when `value.array()` is
   null. Fix independently and add a focused non-array/items regression.

3. **Evaluate a standard archive container instead of two private framing codecs.**
   `src/demi/packages/PackageArchive.cpp:18` and
   `src/demi/assets/AssetPackage.cpp:19` implement separate magic/header/length/
   file framing (607 combined lines, including policy that must remain).
   New candidate: libarchive, BSD-licensed portable streaming C library with
   read/write ZIP and upstream Android build instructions. It could replace
   framing/entry IO, not audited roots, path confinement, hashes, deterministic
   ordering, extraction limits or transactional publication. High effort and
   lower priority: this is an intentional format migration, and a larger
   dependency may cost more than this small codec saves. Disable unnecessary
   formats/filters; demonstrate net deletion and deterministic byte output
   first. Keep current format if that proof fails. No claim that an archive
   library automatically makes extraction secure.
   Sources: [libarchive](https://github.com/libarchive/libarchive),
   [license and streaming API overview](https://github.com/libarchive/libarchive/wiki),
   [Android build instructions](https://github.com/libarchive/libarchive/wiki/BuildInstructions).

## Fallback findings

- **GPU terrain: retain capability fallback, expose main-path failure clearly.**
  `TerrainPatchBatch3D.cpp:380` covers disabled budgets/unavailable programs;
  `:391` covers fields still preparing; `:405` covers unavailable mapped
  programs; `:628` covers incomplete submission with ordinary meshes.
  `:638` clears the optional submission error after retaining fallback coverage.
  `BgfxRenderer3DTerrain.cpp:90` logs changed fallback reasons and `:104`
  publishes counters. This is not wholly silent. Supported fallback does lose
  smooth mapped blending: the maintained terrain page explicitly describes
  this at `tools/package-store/templates/docs/content/terrain.html.twig:97`.
  Retain capability coverage where required by supported devices. For resource
  preparation or explicit cache disablement, consider whether delayed activation
  or an explicitly selected CPU mode would be clearer. Those are design choices,
  not inherent GPU limitations. Android support should follow capability checks,
  not an assumption that every Android device needs this fallback. Distinguish expected capability/preparation
  status from actual shader/allocation/submission errors; qualification of a
  supported primary configuration should assert zero unexpected fallback after
  warm-up, rather than accept visually plausible fallback as success. Current
  single last-reason statistics can hide simultaneous causes; typed counts
  would make this diagnosable without new rendering machinery.

- **Asset loading: catch-all success deserves tightening.**
  `RuntimeAssetService.cpp:403` accepts any type not claimed by a registered
  loader. `:32-88` then reads bytes and reports upload success without decoding
  or creating a runtime object. This is appropriate for intentional source/data
  residency, but a missing visual/physics loader registration can look like a
  successful asset load. It does not retry a loader that failed: eligibility is
  decided by `supports`, so call it a catch-all registration policy rather
  than error recovery. Prefer explicit source-resident type ownership and fail
  unknown resource types. Preserve intentional data/script/text assets.
  `HeadlessVisualAssetLoader.cpp:8` is a separate explicit headless backend and
  should remain; headless success does not qualify visible GPU rendering.

- **Collision: no observed general sphere-to-box fallback.**
  `JoltBodyShape3D.cpp:26` treats zero-length capsules as exact spheres;
  `:76` and `:105` return failure for failed Jolt hull/mesh creation.
  `:110` chooses a box only after model compound/convex/triangle resolution;
  `ColliderAsset3D.cpp:197` supplies the collider asset's size/offset. Preserve
  explicitly authored box collider assets. Investigate any caller that allows
  shape-creation failure to leave an entity collisionless, but do not assert
  that defect without tracing the caller and reproducing it.

- **Cooked shader loading generally fails closed.**
  `CookedShaderLibrary.cpp:39-116` rejects malformed manifests, missing binaries,
  failed programs and missing matching shader entries. `:60` chooses a desktop
  binary for the explicit Noop backend, a legitimate headless test contract.
  `:31` accepts a missing manifest on the documented assumption that source
  projects with authored shaders are cooked before this boundary. Audit that
  invariant at callers; this branch alone is insufficient proof of a defect.

- **Validation defaults are not interchangeable with error masking.**
  Omitted authored defaults belong to component contracts. Unknown schema
  keywords silently ignored by partial interpreters are different: they risk
  promising validation that never executes. Library adoption must explicitly
  choose/validate the dialect and preserve stricter network policy rather than
  silently broadening trust.

## Keep existing well-placed integrations

HTTP already delegates transport to libcurl (`HttpTransfer.cpp:28`), hashing to
mbedTLS (`PackageHash.cpp:7`), simulation to Box2D/Jolt, text shaping to HarfBuzz/
SheenBidi, Unicode to utf8proc and noise to FastNoiseLite. Thin ownership,
limits, cancellation and platform adapters are not redundant reimplementations.
Do not add alternate libraries for those responsibilities.

Terrain erosion is not a quick deletion candidate: the existing
[library evaluation](terrain-erosion-library-evaluation.md) records cancellation,
drainage, thermal and portability gaps. Preserve that evidence; a faster narrow
solver is not equivalent offered functionality. No fresh upstream erosion
qualification was performed for this report.

## Follow-up: minimal cgltf consolidation boundary

Keep both existing public loader APIs and their result types. Add a private
`GltfDocument` owner under assets, backed by a unique owner of `cgltf_data` with
`cgltf_free` deleter. Its minimal operations are parse(path, error),
loadBuffers(error), validate(error), and const data access for the two private
consumers. Parsing separately from buffer loading preserves the cheap accessor
bounds operation; it should not decode full vertex/animation/image content.
Do not introduce a configurable general import pipeline or copy cgltf's entire
API. The existing small attribute lookup helper can live beside the owner.
Use upstream accessor reads and node-transform functions directly inside these
asset implementation files. No cgltf types enter runtime/public headers.

Move the single implementation and existing `demi_cgltf_*` symbol-renaming
definitions out of `GltfSkinnedModel.cpp:1-39` into the adapter implementation,
with matching private declarations shared by consumers. Those prefixes prevent
collisions with bgfx's bundled implementation; simply removing them can create
duplicate symbols. Preserve the current bundled header version initially.

Exact replacement scope:

- `GltfGeometry.cpp:18-91`: delete AccessorView and handwritten identity,
  multiply and JSON TRS decoding in favor of upstream node world transforms
  plus the existing model-import conversion. A point/matrix operation needed
  for applying the import conversion remains an engine concern.
- `GltfGeometry.cpp:102-218`: delete binary file loading, bufferView arithmetic,
  memcpy float/index/UV decoding; use validated cgltf buffers/accessors.
- `GltfGeometry.cpp:220-322`: rewrite primitive and scene traversal against
  cgltf structures, retaining triangle-only selection, default-scene roots,
  indexed/nonindexed topology, UV defaults and output bounds. Traversal/output
  assembly do not disappear just because parsing becomes upstream-owned.
- `GltfGeometry.cpp:331-359`: replace JSON parse/buffer-loading/default-scene
  checks with the owner. Preserve explicit missing/default scene diagnostics.
- `ColliderAssetGenerator.cpp:51-112`: remove duplicated TRS/matrix parsing;
  `:132-244`: replace JSON accessor-bound and scene traversal with cgltf
  structures. Keep the accessor min/max presence requirement and error codes,
  eight-corner bounds expansion, profile conversion, minimum-size policy,
  collider selection, deterministic generated sources and transactional writes.
- `GltfSkinnedModel.cpp:531-545` and final `cleanup()` become owner lifetime.
  Keep skin/joint extraction, inverse bind matrices, normals/weights, embedded
  and external image handling, animation channels, import transform and pose
  evaluation. These provide real functionality cgltf does not replace.

Do not replace static geometry with skinned-loader bind-pose positions. Static
geometry walks only default-scene roots; the skinned loader currently imports
all mesh-owning nodes. Static geometry applies authored node transforms without
skin deformation. `normalizedBounds` uses bind-pose positions, whereas detail=0
`.gltf` bounds use accessor min/max and a separate import-conversion expansion.
Changing those differences is a separate contract decision, not consolidation.
Likewise `ColliderAssetGenerator.cpp:485` performs a separate triangle-geometry
check for positive-detail `.gltf`; retain equivalent validation coverage.

Qualification should include the existing `gltf_skinned_model_tests.cpp`,
`gpu_skinning3d_tests.cpp`, `lightweight_3d_workflow_tests.cpp`,
`editor_asset_workflow_tests.cpp`, `collider_shape_asset_tests.cpp` and
`compound_collider_asset_tests.cpp`. Add a focused static-geometry corpus with:
selected scene versus unused scene nodes; nested matrix/TRS and mirrored scale;
interleaved/accessor-offset data; 8/16/32-bit indices and nonindexed triangles;
UV retention and omitted UV defaults; missing/truncated/out-of-range buffers;
and exact imported bounds/triangle values before and after. Pair a skinned mesh
with deliberately different rest and bind positions so the consumers cannot be
accidentally unified. Verify metadata bounds differ from tight vertex bounds
where expected. GLB/data-URI/sparse accessor support would be newly expanded
static-loader behavior: qualify and document explicitly rather than claim it
was already provided. Run Linux tests and Android compile/device probes for
affected import/cooked consumption; no such qualification ran in this audit.

## Follow-up: smallest data-schema crash fix

`DataDocument.cpp:167` returns `std::get_if<Array>`, confirming the pointer can
be null. Change only the condition at `DataAsset.cpp:156` to
`array != nullptr && schema.contains("items")`.

Place regression cases beside schema-backed validation in
`tests/data_asset_tests.cpp:450`, before its unsupported-schema-version case.
Use schema
`{"format_version":1,"type":"object","properties":{"level":{"items":{"type":"integer"}}}}`
with data `{"format_version":1,"level":4}` and require no errors. The missing
`type` on the level schema is deliberate: adding `type: array` returns early on
the existing type-mismatch check and fails to reproduce the crash. JSON Schema
array keywords are inapplicable to non-arrays. Also exercise `level:[1,2]`
success and `level:[1,"bad"]` failure with `DATA_SCHEMA_TYPE_MISMATCH` at
`#/level/1`, preserving real array validation. The public document schema and
matching manifest dependencies can reuse the existing fixture setup. No
test execution was performed by this audit agent. In a subsequent focused task,
the null guard and these three regression cases were implemented in
`DataAsset.cpp` and `tests/data_asset_tests.cpp`; builds remain deferred while
the parent agent measures the separate ECS migration.

## Bounded glTF implementation status

The approved parser consolidation is now implemented: `GltfDocument.h/.cpp`
owns the single prefixed cgltf implementation and lifetime; `GltfGeometry.cpp`
uses it for static scene geometry and `GltfSkinnedModel.cpp` uses it for existing
skin/clip extraction. The default-scene versus all-nodes policies remain
separate. The collider generator migration is also implemented, replacing its
independent JSON/TRS/scene decoder with the same metadata owner while retaining
the accessor-bound fast path.
Focused tests added to `gltf_skinned_model_tests.cpp` cover selected scenes,
nested mirrored transforms, strided/offset attributes, UV preservation, embedded
GLB buffers with selected-scene traversal, three
index widths, nonindexed/default-UV data, import conversion and malformed buffer
or index rejection. The parent agent owns builds and execution; this status
records source completion only, not a green qualification result. Across the
four changed production C++ files and the two new adapter files, the change adds
259 lines and removes 506 lines: 247 fewer production C++ lines, excluding tests,
documentation and CMake registration. This is a maintenance-size result, not a
performance measurement. Metadata parsing validates declared buffer/view sizes
and references without loading external buffers; runtime buffer reads and
loaded index validation remain in the explicit load step.
