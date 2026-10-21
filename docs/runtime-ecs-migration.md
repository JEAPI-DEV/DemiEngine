# Runtime ECS migration

Work in progress, 2026-10-21. This records implementation and qualification;
it does not promise that replacing storage alone resolves rendering performance.

## Replacement boundary

EnTT 3.15.0 (MIT, archive SHA256 pinned in CMake) owns live typed component pools.
One EntityCollection owns one registry plus ordered entity metadata. Durable
JSON IDs remain strings; native handles never enter scenes, packages, saves or
Lua. The previous per-entity std::any maps and global component-membership and
storage epochs are removed from the runtime.

Typed queries enumerate matching EnTT pools, map handles to authored order,
and merge/deduplicate where needed. They no longer discover membership by
scanning all entity maps or retain a global-epoch membership cache. Ordered
results preserve deterministic simulation order.

The engine still needs ownership and authoring boundaries around the library:

- Detached builders and snapshots own lightweight entt::any values. Binding
  copies them into the destination pools transactionally, then releases staging.
  No detached value map remains alongside a live component row.
- Entity/world copies are independent snapshots. Moves transfer ownership;
  insertion into another collection migrates the row into that collection.
- Explicit collection replacement, erase and swap operations maintain native
  handle-to-order mapping. No implicit mutable-vector conversion is provided.
- Terrain publication pre-adopts replacement/addition rows and sizes index
  storage before changing visible entities. Failed adoption preserves original
  source and live state. Unpublished rows are excluded from ordered queries.
- Registry-local construct/destroy signals and collection-order revisions
  invalidate borrowed addresses. In-place update signals notify value consumers
  without invalidating unrelated addresses. A change in another registry does not invalidate
  this world's component pointers.

An initial implementation gave each detached entity its own registry. Native
qualification exposed roughly 4 GB RSS versus roughly 700 MB before migration.
It was rejected: sparse/component pages belong to the world, not each temporary
builder. Detached values now have no registry until collection publication.

## Controlled prototype

`tools/performance/ecs` compares the former production map with EnTT using
actual Transform3D and MeshRenderer value types. Each backend runs both full
scans and explicit change notifications. Four workloads exercise static scenes,
1% movement, all-object movement, and 1% component removal/re-addition.
All controls must produce identical projected records and visible checksums.

Three alternating-order rounds, 100,000 entities, sixty measured frames after
ten warmup frames gave these medians of per-run median CPU totals:

| Workload | Map scan | EnTT scan | Map tracked | EnTT tracked |
| --- | ---: | ---: | ---: | ---: |
| Static | 8.619 ms | 1.853 ms | 0.649 ms | 0.660 ms |
| 1% moving | 8.387 ms | 1.822 ms | 0.686 ms | 0.701 ms |
| All moving | 14.805 ms | 3.742 ms | 14.619 ms | 4.242 ms |
| 1% structural changes | 8.440 ms | 1.992 ms | 0.876 ms | 0.805 ms |

Peak process RSS was about 100 MiB for the map and 71–72 MiB for EnTT in this
fixture. Notifications are not free: tracked mode loses to a dense scan when
every object moves. Sparse updates avoid unnecessary work even with map storage.
Both choices matter; the table must not be attributed wholly to EnTT.

These totals include mutations, render-record projection and five synthetic
visibility volumes. They exclude hierarchy propagation, actual materials,
shadow submission, physics and GPU rendering. They are not engine frame times
or a claim of 100,000 fully simulated/rendered objects at a particular FPS.
Raw evidence: `/tmp/demi-ecs-results.jsonl`. The reproduction commands and scope
limitations are in `tools/performance/ecs/README.md`.

## Explicit Transform3D mutation and derived world poses

The Transform3D follow-up replaces polling of component values with notified
writes. `Entity::component<Transform3DComponent>()` and the matching storage
getter return const pointers, including on mutable entities. `setComponent`
also returns a const reference for this type. Callers can publish a complete
value with `setComponent`, or edit a staged copy through the explicit boundary:

```cpp
const bool present = entity.updateComponent<Transform3DComponent>(
    [&](Transform3DComponent &transform) {
      transform.position = position;
      transform.rotation = rotation;
    });
```

`updateComponent` returns false when the component is absent and does not invoke
the callback. Otherwise it copies the current value, invokes the callback on
that copy, and commits through component storage. A throwing callback leaves
the live value unchanged. An identical Transform3D replacement succeeds without
emitting an update; comparison covers parent, position, rotation and scale.
Actual live changes publish EnTT update signals. Reads do not mark data dirty.
Do not retain the callback's staged reference after the callback returns.

Physics character/body synchronization, Lua transform setters and look-at,
animation root motion, fragment movement, replicated poses, and reflected field
patching use this boundary. Reflected edits retain the owning component's
invalidation hook and preserve unrelated state. Detached builders and candidate
transaction worlds also use explicit updates or complete-value publication;
collection replacement remains the publication boundary for whole entities.
Low-level integrations operating on the native registry must preserve the same
construct/update/destroy signal contract.

`WorldTransformSnapshot3D` subscribes to Transform3D construction, update and
removal with EnTT reactive storage. It retains copied local values and derived
world poses, not borrowed component pointers. Refresh consumes changed handles,
maps them to authored order and recomposes affected entities and descendants.
Repeated updates coalesce in the pending set; values equal to the retained
snapshot need no recomposition. Registry replacement or collection reordering
forces a rebuild. Parent-reference changes, component presence changes and ID
changes rebuild the hierarchy and resolve all poses. Missing parents and cycles
still produce absent world transforms. Listener lifetime does not keep a dead
world registry alive.

This removes full Transform3D value polling, not every full-world operation.
Entity IDs remain publicly mutable native metadata, so each refresh still scans
IDs to detect same-sized renames. Other component types still expose mutable
access and have not all migrated to notified value updates. Renderer preparation
outside the world-transform snapshot may therefore retain value comparisons or
broader registry-revision invalidation. No general event-driven rendering claim
follows from this transform-specific boundary.

The profiler exposes `Renderer3D.transform_updates` for changed local snapshots
and `Renderer3D.transforms_recomputed` for recomposed poses. These are diagnostic
counts, not measured performance gains. The Linux timings below describe the
preceding storage/system-iteration qualification; the explicit mutation and
reactive-snapshot follow-up requires its own measurements before a performance
comparison is reported.

The library audit is separate: existing cgltf consolidation and a shared schema
validator have concrete deletion opportunities. No new general schema engine or
archive format is installed by this ECS change.

## Integrated Linux qualification

The production migration also changes five systems from all-world scans to
matching typed pools: audio source/listener updates, sprite animation, animation
state machines/model playback, animation collision sources/receivers, and 2D
cameras. Queries preserve authored order and enabled-state checks. Model-only
animation playback remains included even without a state machine. This removes
work from systems that have no matching components in a large 3D scene.

Release, Vulkan/SDL3 X11, terrain_3d at 2560 x 1440, one native process at a time,
no concurrent build during timing. Each run used 900 frames; samples 180–899:

| Scope | Map control A median / p95 | Map control repeat median / p95 | Final median / p95 |
| --- | ---: | ---: | ---: |
| CPU frame total | 12.515 / 13.912 ms | 11.554 / 12.535 ms | 9.372 / 10.561 ms |
| Simulation update | 2.774 / 4.054 ms | 2.491 / 3.014 ms | 0.599 / 0.725 ms |
| CPU render preparation | 9.304 / 10.380 ms | 8.767 / 9.461 ms | 8.322 / 9.373 ms |
| Resident process memory | 702.8 MiB | 664.2 MiB | 659.4 MiB |

All selected frames retained 116,888 main triangles and 423,124 shadow triangles.
Settings, camera and density were unchanged. Control variation is shown rather
than attributing all differences to the migration. The major reproducible gain
is simulation iteration; render submission remains the largest cost. Bounded
standalone runs disable audio, so these measurements do not establish editor UI
or active audio-device performance. They are real engine CPU frame measurements,
not the 100k-object prototype's synthetic timing.

Evidence: `/tmp/demi-entt-native-before.csv`,
`/tmp/demi-entt-native-control-repeat.csv`, `/tmp/demi-foundations-final.csv`.
The corrected storage-only intermediate was `/tmp/demi-entt-native-final.csv`;
it improved update time but left total time close to the repeated control.
The subsequent typed system loops provide the additional gain above.

Qualification passed 32 distinct focused suites spanning storage/collections,
lookup/hierarchy, runtime scene/prefab/lifetime, editor workspace/Play/commands/
gizmos, terrain updates/ownership/patch batching, 2D/3D physics, destruction,
network prediction, data validation, static/skinned import, skinning rendering,
audio, camera and animation behavior. Amber Pass native riding E2E passed after
the final importer/system changes. Terrain and Amber Pass validated, and the
minimal 2D Android example passed Android-target validation plus a Linux headless
smoke. This does not claim a device run, Windows qualification, or a full-suite
result. No online documentation or packages were deployed.

## Transform notification follow-up qualification

Release, Vulkan/SDL3 X11, same terrain_3d camera and 2560 x 1440 window,
900 frames with samples 180–899; one engine process and no concurrent builds.
The new trace is `/tmp/demi-transform-events-final.csv`. Comparison is with the
previous recorded run, not an alternating same-session binary comparison.

| Scope | Previous median / p95 | Follow-up median / p95 |
| --- | ---: | ---: |
| CPU frame | 9.372 / 10.561 ms | 9.499 / 10.533 ms |
| Simulation update | 0.599 / 0.725 ms | 0.609 / 0.761 ms |
| CPU render preparation | 8.322 / 9.373 ms | 8.392 / 9.358 ms |
| World transforms | 0.507 / 0.657 ms | 0.770 / 1.020 ms |

No overall speedup is demonstrated. The transform scope increased despite zero
steady-state recompositions; the mutable-ID scan remains. This change establishes
an enforceable mutation contract and selective hierarchy propagation, not a
completed rendering optimization. Main/shadow counts remain 116,888/423,124.

The full native build passed. Full CTest initially passed 442 of 449 tests;
after correcting six test fixtures/expectations, all six passed on rerun.
The remaining Android packaging test is blocked by the existing libuv/API-level
mismatch described in [Android qualification](libuv-android-qualification.md).
Thus 448 distinct tests passed across the full run and focused rerun, not a
single all-green run. Amber Pass native mounted-riding E2E also passed.
Mutation tests cover no-op suppression, callback failure, retained detached
addresses, independent consumers, hierarchy propagation and registry lifetime.
