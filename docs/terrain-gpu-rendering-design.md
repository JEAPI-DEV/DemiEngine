# Instanced GPU terrain proposal

Status: compatible GPU patch path implemented, 2026-10-12. Native measurement
and visual evidence are recorded in [GPU qualification](terrain-gpu-rendering-qualification.md).
This architecture record also distinguishes the remaining delivery work.

## Problem and decision

Current terrain rendering owns separate expanded meshes for chunks, biomes and
material sections. Lazy CPU/GPU residency bounds unused reduced buffers, but
unique mesh identities and section draws still prevent useful instancing.
Replacing their vertices with a height sampler alone would retain those draws.

The renderer implements shared regular patches, vertex height sampling, patch
instancing and per-pixel material routing together. This delivery supports compatible
built-in opaque materials with solid colors and metallic/roughness values,
including the current landscape palette. It reuses the existing lighting and
shadow behavior. Hardware tessellation, compute-generated geometry and indirect
submission are not dependencies.

Intentional visual changes are allowed during alpha/beta. GPU terrain samples
biome and surface-role maps per pixel instead of reproducing the previous
triangle-majority material partition. Map sampling is discrete; smooth blending requires meaningful weights and
remains a separate delivery. This is an intentional alpha/beta visual change,
not a second authored partition or compatibility alias.

## Shared responsibilities

- `runtime/terrain` retains immutable heightfields, classification, material
  bindings, chunk bounds, geometric errors and geometry identity. Prepared
  snapshots remain shared across asset instances. Picking and Full collision
  continue consuming this data independently of render residency.
- `render/bgfx3d/TerrainPatchTopology3D` owns reusable sample-grid geometry keyed
  by patch dimensions and LOD. Reuse the corrected boundary refinement: every
  level retains all Full perimeter samples, including odd and partial chunks.
  Normal sampling uses the shared field at boundaries. Skirts are optional
  supplemental geometry, never the seam guarantee.
- `render/bgfx3d/TerrainGpuFieldCache3D` owns height/classification/palette textures,
  asynchronous CPU preparation, transactional channel uploads and bounded
  residency. Changed channels are replaced as whole textures; dirty-region
  uploads are not implemented. Replacement resources become
  visible together; cancellation or failure retains the previous ready state.
- `render/bgfx3d/TerrainPatchBatch3D` gathers visible patches and groups instances
  by field, topology and compatible shader/material resources. Instance
  records carry transforms, sample origin and metadata offsets. Shared assets
  can share texture/topology resources while retaining different transforms.
- `render/backend` owns texture-format capabilities, generic instance records,
  allocation and commands. Generic instance payloads support the terrain patch
  record alongside existing transform records; bgfx types stay outside terrain
  domain code.
- Terrain vertex/fragment programs own displacement and material-map sampling.
  Common lighting is shared through shader includes. Main and shadow programs
  use the same displacement and palette-aware culling.

A field/material palette is shared by its patches. A lookup maps biome/role IDs
into tinted color, metallic/roughness, unlit and culling entries; explicit biome overrides
replace automatic role bindings in that biome. Palette size and storage are
allocated from content, not a fixed array of terrain roles. Appearance changes
update palette data without replacing heights or patch geometry.

Heights use R32F, canonical normals RGBA32F, palette indices exact little-endian
RGBA8 and palette values RGBA32F. Nearest sampling preserves sample/ID identity.
Texture dimensions, instance batches and work queues follow actual device
limits and configurable memory/work budgets. Do not impose arbitrary biome,
role, field-size or chunk-count caps.

## Capability and material fallback

Local bgfx headers expose `BGFX_CAPS_INSTANCING`,
`BGFX_CAPS_FORMAT_TEXTURE_VERTEX`, R16F/R32F formats, texture arrays and texture,
layer and sampler limits. DemiEngine now exposes R32F textures, generic instance
payloads and the instancing/sampler/vertex-texture checks used by this path.

Unsupported hardware or materials use the existing shared mesh renderer with
the same current authoring contract. In particular, custom material programs,
transparent surfaces, alpha-tested shadows and textured materials must retain
supported behavior until their GPU-terrain equivalents are implemented. Do not
silently substitute a default palette material. Fallback is decided for the whole field, avoiding holes or duplicate surfaces.
Reasons are exposed in diagnostics and profiler counters.

The existing shader build produces SPIR-V, GLSL and ESSL variants. Linux and
Android Vulkan fit that architecture; physical Android qualification is still
required. Native Direct3D selection and Windows shader backend integration are
not currently exposed. Vulkan-compatible design is not Windows qualification.

Mixed `CullClockwise` and `CullNone` entries use a two-sided batch and a per-pixel
palette flag. With pinned bgfx's clockwise front-face convention, CW culling
removes `gl_FrontFacing` fragments. The same test runs before main/shadow
branching. Other incompatible depth/blend states still require fallback.

`Environment3D.terrain_gpu_cache_mb` defaults to 128 MiB for field textures and
shared patch buffers. Zero selects ordinary mesh rendering. CPU field packing
uses the borrowed engine worker pool with staging/outstanding-job admission;
zero workers cannot execute packing inline on the frame thread. Pausing
preparation jobs/staging retains already ready, material-valid bundles. Until a bundle
is ready, or when a budget/capability/material rejects it, Full mesh rendering
remains available. Appearance updates reuse unchanged immutable height/normal
channels and identical encoded classification; replacements never mutate a
ready texture in place. These budgets do not bound total process memory.

## Correctness and qualification

Regression responsibilities for this path:

- Shared topology, sample addressing and normals across every mixed-level edge;
  odd/partial patches, transformed owners and nonuniform scale.
- Per-pixel biome/role lookup, explicit overrides, palette updates, stable
  identities, exact ID encoding and unsupported-material fallback.
- Batch grouping that combines compatible patches without mixing fields,
  transforms, materials or camera states; exhaustion splits legal batches.
- Main/shadow displacement parity, cutout fallback and correct bounds.
- Source edits and Undo/Redo, shared asset instances, cancellation, stale jobs,
  resource eviction and renderer/device lifecycle recovery.
- Unchanged Full collision, field queries and editor picking during GPU LOD,
  uploads and eviction. Render residency must not disable collision entities.

The first native profile probe uses the existing 500x500-cell landscape and
its current palette, with unchanged source resolution, camera, light/shadow
configuration and window size. Compare current lazy mesh rendering against GPU
patch rendering in separate, newly launched `linux-release` processes. Include
a Full-detail run: submission reduction must remain useful when the tight pixel
budget admits no coarser geometry.

Retain warm frame-time median/p95/p99, CPU render preparation, GPU time,
main/shadow submissions, triangles, upload bytes, resident resource bytes and
fallback counts. Then run a repeatable moving-camera probe that exercises patch
LOD and resource residency. Inspect native captures for seams, material edges,
lighting and shadow parity. Define Android quality/resource settings from a
physical-device run; do not infer mobile limits from native Debug timings.

Native evidence confirms lower compatible-terrain submissions; see qualification
for workload, repeat measurements and their limits. Remaining work includes
dirty-region uploads/field paging, collision admission and CPU collision paging,
terrain PBR texture layers, smooth material blending, vegetation and coordinated
water residency. Full CPU surface/collision publication remains resident. This
render delivery does not complete the whole terrain milestone.
