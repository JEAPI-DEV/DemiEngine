# Terrain water publication

## Initial publication boundary

`TerrainWaterMesh` validates and expands prepared indexed water surfaces into
native triangle meshes. Terrain publication owns their lifecycle alongside
ground chunks; renderer and physics policies remain in their existing owners.
Water uses the shared transparent MeshRenderer path with direct lighting,
roughness 0.12, opacity 0.65 and a blue tint. Transparent ordering and disabled
depth writes are the ordinary renderer behavior, not a terrain-only draw pass.

There is one native child per drawable body. IDs encode the stable body ID
reversibly under its terrain owner. Children inherit placement, scene, layer,
persistence and enabled state. They have no collider or rigidbody. Native-only
ownership lets editor picking resolve the authored terrain; generated children
do not enter source scene JSON or create asset files.

Full loading, prepared asset loading, local updates, shared placement batches,
history restoration, cancellation and owner removal use the same boundary.
Appearance-only changes compare retained water data and avoid replacing water
geometry. Surface comparison is allocation-free and has a shared-artifact
identity fast path. Changes stage mesh allocations and ID-conflict checks before
the existing atomic commit. No per-frame water generation was added.

Prepared payloads already retain water position, normal, index and depth arrays;
this publication change needs no new source or binary format. Loading does not
run noise, erosion or carving. Depth is validated and preserved but is not yet
consumed by the shader.

## Initial publication qualification

Release editor/CLI and the affected test targets build. The final focused gate
passes all nineteen checks in 4.88 seconds: water mesh/world, carving and queries,
cook/cook-node, graph, ground world/world-update, update, generator, layers,
preset, asset/asset-world/asset-pipeline/asset-runtime-service, and editor terrain
graph-settings/asset. Both terrain example projects validate without diagnostics;
`git diff --check` is clean. This is a scoped gate, not the full repository suite.

Focused water mesh/world tests cover native metadata and IDs, invalid geometry,
empty bodies, cancellation, source/prepared loading parity, visibility, owner
removal, water-level changes, shared placement updates, appearance-only retention,
history restoration, water-output removal and rollback when a later placement
conflicts with an authored entity's ID. Existing water queries, carving, cook,
graph and ground world/update checks remain part of the focused gate.

A separate 16-unit source asset was imported through the CLI and cooked outside
the source project. Its prepared scene runs headlessly for three frames with
five renderable entities (four ground chunks and one water surface).
PID/title-verified native Vulkan editor captures at 5120×2806 show water in source
and cooked previews, without console diagnostics. The cooked Inspector remains
read-only. Source probe geometry is deliberately simple and not a landscape
quality claim. Public documentation templates are updated, not deployed.

## Shoreline and depth appearance extension

The initial screenshot was an unsuitable containment probe: its level was 5
while surrounding terrain was 3, so its finite boundary could not meet banks.
It also exposed whole-cell shoreline overshoot. The updated probe sculpts a
basin into ground above level, rather than concealing the issue by lowering
the native water mesh independently of its authored level and gameplay queries.

Water surface construction clips the terrain's two triangles per cell against
signed ground depth and body ownership. Shared-edge intersections are canonical
and zero-depth shore vertices lie on the water plane. Degenerate triangles are
discarded. Water queries use the same triangle weights and ownership boundary,
without inspecting a rendered mesh. Mesh LOD/culling cannot remove gameplay
water. Bounded lakes with low banks at their authored boundary produce a graph
warning after Generate; automatic flooding or invented banks are not implied.

Water Body graph controls provide normalized shallow/deep RGBA, positive
absorption distance and normalized roughness. Depth-derived colours approach
the deep colour exponentially. This is an artistic colour/opacity approximation,
not spectral absorption, refraction or screen-depth rendering. At zero depth
the colour equals the authored shallow endpoint. Colours are computed when
publishing meshes, not allocated every frame.

The shared MeshRenderer contract adds `vertex_colors` and native ColorArray
reflection/serialization/Inspector controls. Buffers are validated against
vertex count and normalized finite RGBA. Native generators mark geometry
changed; reflected mutations validate against retained live buffers and invoke
invalidation. GPU RGBA packing occurs on cache refresh and dent refinement keeps
the colours. This is reusable inline-mesh functionality, not a terrain-only
shader branch. Public Lua creation metadata and canonical component schema agree.

Terrain generator v3 invalidates old source caches. Terrain asset envelope v3
stores body appearance and requires old shipped payloads to be recooked; there
is no alpha compatibility reader. The underlying cooked-field version remains 3.
Earlier binding/publication evidence above used envelope v2.

### Extension qualification

Follow-up inspection of the actual, user-edited `terrain_graph_3d` recipe
distinguishes publication from useful placement: on the current 2000×2000,
500-cell landscape, the saved level-9.2 lake publishes only six vertices near
local X89–90/Z63–65. Its river publishes 198 vertices near local X22–64/Z72–126.
With the terrain's (-64,0,-64) placement these are at the original corner, not
the later sculpted basin beside the cube around world (927,909). The water nodes
retain coordinates from the smaller landscape. Current lake carving deepens
already-wet ground; it does not excavate a new depression in above-level land.
The small probe's success therefore did not qualify useful water coverage or
placement in the user's edited example. A native example-source publication
probe now prints each body's actual bounds; source settings remain unchanged.

After the user approved relocation, the authored lake was moved to local
(991,973), or world (927,909), with a 180-unit footprint. Both bodies use level
6; the river path starts in the lake and follows the basin southwest. Sculpt,
paint and cube data are unchanged. The example camera now targets the basin.
The native example-source probe publishes 15,744 lake vertices and 702 river
vertices, and the water query at the basin centre reports depth 2.14923 units.
An actual example editor capture shows the lake in its sculpted banks. Reimport,
source validation, cooking and three-frame prepared headless loading succeed.
The water-world, water-query and graph checks pass (three checks, 7.78 seconds);
`git diff --check` is clean. The maintained terrain guide includes the actual
example capture, not only the small synthetic probe.

Release editor/CLI and affected test targets build. Fourteen focused checks pass
in 4.69 seconds: structured-value and Inspector model, scene loader, runtime
object model, component schema, renderer3D, water mesh/world/carving/queries,
graph, terrain world/world-update and terrain asset. The canonical schema was
regenerated through `demi schema export`; both terrain examples validate without
diagnostics and `git diff --check` is clean. These are scoped checks, not the
full repository suite. The graph example can produce a containment warning
during Generate; project source validation does not evaluate hydrology.

The updated source probe sculpts a basin into height-7 land and fills it at 5.
It reimports and cooks successfully, and its prepared scene runs headlessly for
three frames with five renderable entities. PID/title-verified native Vulkan
captures of source and cooked projects at 5120×2806 show matching water meeting
the banks, with no console errors. The public screenshot now shows this contained
probe, not the earlier raised finite disc. The RGBA widget regression includes
keyboard activation, independent alpha editing and commit. Website templates
are updated locally, not deployed.

## Connected lake coverage

Lake candidates now retain only the positive-depth component nearest their
authored centre. The iterative flood fill follows the six edges of the terrain
triangulation; it does not join basins through dry diagonal corners or land
exactly at the water level. A one-edge dry halo supports clipped shores, and
exact waterline samples remain eligible for shore softening without becoming
connectivity bridges. Ocean coverage and authored river paths retain their
existing policies.

Carving filters disconnected lake samples before modifying them. Final surface
refresh recomputes selection after terrain/river edits. Results retain immutable
coverage shared with queries. Query contexts consume that prepared selection;
rendering does not flood fill. Native manually constructed results without a
selection can build one at context creation, not per sample. Cooking stores
ownership indices and restores wet flags and levels from the saved body table,
with strict grid/index/level validation. Terrain generator and asset envelope
are v4; older prepared files need recooking.

Connectivity is linear in grid samples plus visited triangle edges, with no
recursive traversal, per-frame search or arbitrary body/sample cap. Scoped
tests cover separated basins, a connecting channel, ocean behaviour, cancellation
and prepared selection round-trip/malformed data. These are internal
implementation and qualification details; the public terrain guide describes
only centre selection, connected coverage and the authoring workflow.

The final Release gate passes all six focused checks in 7.80 seconds: water
world, water carving/connectivity, water queries, graph, world-update and terrain
asset. Source validation and cooked three-frame headless loading pass. A
single sequential before/after run of the same water-world probe measured
3.879 seconds before and 3.875 seconds after, including the unchanged 500×500-cell
example generation and small fixtures. This is a coarse generation sanity check,
not a frame-time or statistically qualified performance claim. The lake changes
from 15,744 to 14,517 vertices; river vertices and centre depth are unchanged.

## Remaining scope

This is the first visible publication stage, not completion of milestone 5.
Reflection/refraction, waves, foam, underwater camera effects and buoyancy/
swimming remain pending. Footprint ownership remains grid-derived and ground
intersections are piecewise linear; this is not a spline shoreline tessellator.
Whole-body meshes do not provide chunked water
LOD or streaming. No frame-time, large-world or Android hardware qualification
is claimed by these checks.
