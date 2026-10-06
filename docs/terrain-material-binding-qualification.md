# Terrain material binding qualification

## Delivered boundary

Terrain biomes bind ordinary Material assets through the same MeshRenderer and
MaterialLibrary used by other objects. Majority-biome triangle groups remain
discrete; no layered blending or high-quality source art is implied.
`texture_scale` is positive finite repeats per terrain-local unit, default 1.
UVs use global lattice positions, so chunk boundaries agree. The texture asset
must use Repeat wrapping for repeated sampling. Biome colours remain tints.

The field palette carries material IDs and texture scales. Full publication,
local updates, shared placements and rollback use that same metadata. Material
changes retain geometry/collision; scale changes rewrite UVs and advance the GPU
revision without re-running height generation or replacing collision. The patch
palette includes appearance for Undo/Redo. Renderer resources remain native
children, not developer-facing generated asset sources.

Appearance asset contents are excluded from direct biome generation inputs.
References remain in recipe identity and cook dependencies. A material also
referenced by a generation/scatter input remains part of that input's fingerprint.
The separate process-wide inline generation cache still keys on the full recipe;
reloading an inline recipe after an appearance edit can miss that cache. The
incremental graph workflow and prepared-asset publication are the qualified
no-regeneration paths here.
Prepared cook v3 and terrain asset envelope v2 retain appearance without source
recipes. Old derived payloads require recooking; there is no compatibility reader.
Cooked terrain can be viewed in the editor but graph/brush authoring is read-only,
with a clear source-project diagnostic instead of trying to parse binary JSON.

Graph controls offer ordinary Material assets, reset scale by restoring omission,
and retain one draft/history. Typed terrain_material and terrain_material_set
definitions remain authorable, but assignment here produces a validation warning
because they are not renderable in this path. Other invalid asset types or missing
references are errors.

## Qualification

Release editor/CLI and the affected test targets build. The final focused gate
passes all fifteen checks (4.94 seconds): terrain cook/cook-node, asset,
asset-world, asset-pipeline, asset-runtime-service, world/world-update, update,
generator, graph, layers, preset and editor terrain graph-settings/asset.
The graph UI cases cover repeated scalar editing and reset through real keyboard
navigation. Prepared-terrain authoring has an explicit read-only regression.
`terrain_3d` (25 files) and `terrain_graph_3d` (4 files) validate without diagnostics;
`git diff --check` is clean.

Focused runtime checks cover material updates, UV continuity, retained colliders,
multiple owners, stale publication, rollback and source-free asset loading. Cook
checks cover round-trip appearance columns and malformed/truncated data. The
graph controls use actual ImGui keyboard navigation; the open TreeNode scope and
reactivation after committing a scalar are part of the fixture.

A separate 20×20-unit, 16×16-cell checker terrain was imported and cooked outside
the source project. The cooked scene runs headlessly for three frames with sixteen
native render surfaces and a .terrain.bin source. PID/title-verified native Vulkan
captures of source and cooked data show the same tiled appearance; the cooked
Inspector is read-only and has no preview rebuild error. This verifies a binding
probe, not landscape art quality, frame-time performance, Android hardware or
arbitrary material/shader fidelity. Website templates are updated, not deployed.

Remaining milestone-4 work includes typed terrain PBR maps, role-set application,
smooth/height-aware layer blending, triplanar mapping, residency and licensed
landscape art. The subsequent native-water publication slice is recorded in
[terrain water publication](terrain-water-publication-qualification.md);
advanced water shading remains pending in milestone 5.
