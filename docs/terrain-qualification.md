# Finite terrain authoring qualification

Public workflow: `tools/package-store/templates/docs/content/terrain.html.twig`.
Remaining scope: [terrain-plan.md](../terrain-plan.md).

## Implemented ownership

- `TerrainRecipe` owns parsing/defaults and validation; the reflected Terrain3D
  component stores the authored recipe and an immutable generated-field handle.
- `TerrainGenerator` uses pinned FastNoiseLite and replays radial edits over the
  procedural base. Samples and normals are global to a finite heightfield, so
  chunk boundaries agree. Float-resolution validity is checked explicitly.
- `TerrainWorld` creates native-only chunk meshes and matching static triangle
  colliders. Scene loading and the editor use this same path. A terrain-owned
  index synchronizes generated visibility without scanning unrelated worlds
  on every frame; scene activation/unload rebuilds ownership.
- `TerrainGenerationCache` weakly deduplicates canonical recipes and validates
  published field structure. Worlds/controllers retain memory, not the cache.
- `EditorTerrainAuthoring` owns drafts, strokes and cancellable generation.
  The workspace commits completed recipes through its existing reversible
  commands, rejecting stale results. Native generator data is never saved as
  source meshes. Structured field commands preserve numeric payload precision.
- `TerrainSurface` shares triangle-consistent sampling and grid-traversal ray
  picking. The editor uses common viewport camera rays and terrain transforms.
  Generated chunks redirect to the authored owner and stay out of the authored
  hierarchy. Framing the owner includes the full generated surface.

## Linux checks, 2026-09-28

Release CLI/runtime/editor built. Focused tests cover generation, edit replay,
protection, invalid data, cancellation, cache structure/ownership/concurrency,
ray queries, mesh/collider parity, transformed physics raycasts, editor drafts,
strokes, stale jobs, Undo/Redo, Save/reload and source/generated separation.
Related scene-document, prefab-component, runtime-object-model, embedded-play
and canonical-schema regression checks passed during qualification.

The `terrain_3d` example validates for Linux and Android target data. It was
visibly rendered with Vulkan at 1280x720; the native editor also opened and
displayed the terrain and Inspector without diagnostics. Synthetic desktop
clicks did not reliably reach this Wayland/X11 session, so interactive brush
feel and dropdown operation still merit user verification. Automated tests
exercise the real controller/workspace paths, not just recipe mocks.

The maintained website Lua-service name check passes (817 references). PHP
template validation and a physical Android/device performance run were not
performed. Capability checking reports additive changes, including earlier
task/database/shadow changes; no unrelated baseline rewrite was made here.

## Surface-query measurement

`build/linux-release/demi-terrain-surface-tests` compares the same 250 seeded
rays against an exhaustive triangle reference on a 128-by-96-cell heightfield.
A Release run measured 167.16 ms total for exhaustive queries and 0.464 ms for
grid traversal. All hit/miss and intersection checks passed. This is a scoped
CPU query comparison, not terrain FPS, GPU timing or large-world qualification;
desktop background load and CPU frequency were not controlled.

## Remaining limits

This is finite heightfield terrain. Current edits rebuild the full heightfield;
background generation is followed by main-thread scene/mesh publication.
Large recipes can still hitch during publication and GPU/physics upload.
Biome tints are discrete triangle groups, not blended material layers. Reusable
biome assets, vegetation scattering, cooked terrain caches, dirty-region
generation, LOD, streaming and volumetric caves remain explicit follow-ups.

Protection freezes absolute samples and requires the original grid. Resizing
requires an explicit keep/clear decision; protected grids cannot be silently
remapped. Placed objects are not automatically moved to follow regenerated land.
