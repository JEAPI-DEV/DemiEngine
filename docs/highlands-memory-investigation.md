# Highland memory investigation

2026-10-23, Linux Release, native Vulkan at 5120x2880. This is a diagnosis,
not a memory optimization or a frame-time benchmark.

## Method

A diagnostic preload sampled glibc mallinfo2 once per second. An external
monitor sampled /proc/PID/smaps_rollup and process status at the same rate and
watched available system memory. No kernel/cgroup memory limit was applied.
The monitor would stop the owned run if available memory fell below 8 GiB or
process RSS exceeded 8 GiB; neither condition triggered. No builds or second
engine instance ran concurrently.

The existing native E2E traversed Silvermere -> highlands -> Silvermere. Its
final wait was temporarily extended to twelve seconds for post-return samples,
then restored byte-for-byte. All three tests passed. A diagnostic-only call to
malloc_trim(0), three seconds after the return, tested whether RSS was retained
free allocator storage rather than still-live scene objects. It is not installed
in the engine and is not proposed as an every-frame cleanup mechanism.

## Measured results

Units below are binary MiB/GiB, unlike the earlier rounded decimal GB figures.

| Checkpoint | RSS | Live glibc allocations | Free glibc arena storage |
| --- | ---: | ---: | ---: |
| Sampled peak, highland loading | 4639 MiB | 2185 MiB | 2186 MiB |
| Immediately before diagnostic trim | 4589 MiB | 340 MiB | 3734 MiB |
| First sample after trim | 1150 MiB | 339 MiB | 3735 MiB |

Trimming reduced physical residency by roughly 3439 MiB (3.36 GiB) without
reducing live allocation bytes. The arena's virtual free capacity remains in
mallinfo2 after pages are released, which is why its free-byte column does not
fall. Ordinary rendering subsequently touched some pages again; RSS reached
about 1210 MiB before exit. RSS is not identical to malloc accounting: it also
includes stacks, executable/library mappings and driver mappings. These numbers
must not be added together as separate complete memory categories.

This demonstrates that most of the post-return RSS was freed heap storage still
resident in the allocator. It does not prove that every live allocation is
necessary or rule out unrelated leaks.

## Allocation source

The earlier short heap trace reached a 1.95 GB peak tracked heap during terrain
preparation. It was too intrusive for the ordinary scene-wait timeouts, so its
functional tests timed out; use it only as allocation evidence, not as a passing
scene-transition run. It recorded about 85.4 million allocation calls.

Dominant stacks enter terrain_payload::decode through terrain_storage::read and
prepareTerrainAssets. A current highland cache file is about 298.74 MiB:
32.08 MiB final field, 32.08 MiB edit checkpoint, and 234.59 MiB JSON derived data.
The format is nominally binary but this large derived section remains JSON.

TerrainAssetPayload.cpp holds the whole input byte buffer, copies the JSON
section into a string, parses a complete nlohmann DOM, then materializes the
native data while that DOM is still alive. TerrainAssetPayloadData.cpp includes
final scatter placements, base placement candidates, water vertices/normals/
indices and sample classifications in this section. Each small JSON object and
array adds allocations beyond its numeric or text payload. The trace identifies
hundreds of megabytes of JSON map nodes and additional buffers/arrays/strings;
individual allocator-category peaks are not summed as a simultaneous total.

Development startup prepares all registered terrain assets, and checking an
existing prepared cache currently calls terrain_storage::read, fully decoding
it. Thus a warm source-cache check can still perform the large temporary decode
before the user enters the highland scene. Later background scene preparation
can exercise different allocator arenas. Source/generation field caches checked
in this investigation use weak pointers: they are not evidence of strong world
retention.

Separately, a materialized 1024x1024-cell terrain contains 2,097,152 triangles:
192 MiB of expanded Full render positions/normals/UVs, 72 MiB of native collision
triangles, and potentially 24 MiB of material-section indices, before capacities,
physics acceleration structures, vegetation, render resources and temporaries.
Those are real costs but do not explain multi-gigabyte retained free arena space.

## Fix direction

1. Encode bulk derived terrain arrays/placements compactly and decode directly
   into native storage, preserving strict checksums, dimensions and identity.
2. Avoid copying the entire metadata section and avoid full object materialization
   when validating an unchanged prepared cache; retain corruption checks.
3. Separate runtime-required data from editor checkpoints where the shipping
   contract permits it, without discarding editing or undo data from source.
4. Then address eager Full geometry/collision and camera-driven residency. Do not
   substitute a larger budget or a recurring trim call for fixing allocation churn.

## Profiling incident and artifacts

The attempted extended Heaptrack interpretation triggered a real OOM event at
22:36:09 Europe/Berlin: heaptrack_interpret reached about 16.9 GiB anonymous RSS.
The kernel killed it and its shared VS Code scope failed. That extended trace is
incomplete and must not be treated as qualification. No profiler processes remain.

Local diagnostic artifacts are under /tmp/demi-memory-investigation:
monitored.log (timestamped scene transitions), monitored-malloc.csv,
monitored-maps.jsonl, baseline-malloc.csv, baseline-maps.jsonl and heap-report.txt.
The temporary sampler/monitor source is in the same folder. These are local
investigation artifacts, not shipped assets or persistent engine configuration.
