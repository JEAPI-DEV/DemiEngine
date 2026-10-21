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
- Registry-local construct/destroy/update signals and collection-order revisions
  invalidate borrowed addresses. A change in another registry does not invalidate
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

## Mutation boundary still to migrate

Native component getters still expose mutable references for compatibility with
existing systems. Those writes cannot emit EnTT update notifications by magic.
Value-sensitive transform consumers therefore still compare live values.
Removing those checks before migrating writers would cause stale rendering.

The bounded next step is an explicit, notified component-update operation,
followed by const Transform3D reads enforced at compile time. Persistent writers
include physics character/body synchronization, Lua transform setters and
look-at, animation root motion, fragment movement, replication and reflected
field patches. Detached builders and unpublished transaction worlds have a
separate lifecycle. Once those writers are migrated, transform-specific dirty
sets and descendant invalidation can replace full transform polling.

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
