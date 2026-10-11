# Terrain surface material integration

## Delivered boundary

A format-2 terrain palette's material set now assigns ordinary Material assets
through surface-role classification. The same final heightfield and finite
water-body coverage drive source previews and cooked runtime output. Explicit
biome materials win; biome colors remain material tints. Missing bindings and
unassigned samples retain the biome tint and appear as Terrain Graph warnings.

This is discrete triangle assignment, not blended terrain shading. Triangle
vertices vote by role; three distinct roles use the first vertex. Smooth
blending, triplanar projection, terrain PBR texture maps and vegetation
instancing remain separate integration gates. Existing material scorer gaps
remain explicit unassigned samples rather than being silently reassigned.

The public recipe, palette and material-set versions are unchanged. Terrain
asset-envelope and generator versions advance to 6, invalidating generated
caches. Reimport and recook before shipping.

## Ownership and publication

- `TerrainMaskBuilder` owns complete derived masks and finite water coverage.
  The pipeline and surface classifier share it. Water distances reuse the
  drainage distance implementation; resolved body depth overrides inferred sea
  level for material decisions.
- `TerrainSurfaceMaterials` owns immutable classification and material bindings.
  Binding-only changes reuse classification and leave height samples untouched.
- `TerrainMaterialSections` groups indices against existing terrain vertices.
  Native `MeshMaterialSections3D` is runtime draw data, not an authored component.
  Section bindings can change without replacing GPU indices or collision meshes.
- Terrain updates preserve these snapshots through reversible patches. Geometry
  publication must not overwrite a newly rebuilt section table with bindings
  from the old live table. Regression coverage includes adding/removing biome
  overrides, material rebinding, sculpting and Undo/Redo.
- Cooked payload validation checks classification dimensions, role names,
  duplicate role entries and sample indices. The empty role is the scorer's
  explicit unassigned state; it must survive round trips.

## Executed qualification, 2026-10-11

Sixteen focused suites passed: bgfx renderer3d; terrain material layers,
pipeline, update, world update, asset, drainage, water, graph, asset validation,
cache; editor terrain authoring and history; physics3D, transform hierarchy and
material library. New regression cases exercise:

- finite lakes above and below the inferred sea plane, with dry exterior samples;
- binding-only classification reuse, cancellation preserving the prior snapshot;
- material section index ranges and GPU vertex-upload reuse;
- explicit biome override transitions without collision replacement;
- sculpt reclassification and restoration through history;
- cooked classification/binding parity, unassigned roles and invalid indices.

`demi validate examples/terrain_graph_3d` checked 15 files without diagnostics.
`demi validate examples/terrain_3d` checked 26 files without diagnostics. The
branching example now supplies all nine ordinary Material bindings in a
surface-only palette. Existing terrain geometry, authored biome colors, water,
scene objects and full-density meadow placement rules were retained.

The branching example cooked to `/tmp/demi-terrain-material-cooked`. Validation
of that output checked 14 files without diagnostics. A three-frame headless run
completed with 1,229 renderables. No surface proxy objects were added.

Native X11/Vulkan editor checks used a 5120 x 2806 client on the external display.
Opened Terrain Graph, inspected the unassigned-surface warning, returned to the
viewport, zoomed into the terrain, started embedded Play, and stopped it again.
Screenshots were inspected at each step:

- `/tmp/terrain-material-viewport-full.png`: automatic surface regions in the editor;
- `/tmp/terrain-material-graph-full.png`: graph and actionable fallback warning;
- `/tmp/terrain-material-play-full.png`: embedded runtime with the same landscape.

These checks establish Linux rendering and persistence. They do not qualify
Windows, Android, smooth surface transitions or high-density vegetation.

## Performance follow-up

The user reported roughly 1–5 FPS and 123 ms average update time. A native
unoptimized Debug runtime reproduced the CPU bottleneck. See
[terrain performance qualification](terrain-performance-qualification.md) for
same-build improvements and the separate optimized-build comparison. The
optimized editor was then checked at the same full display resolution.

An unintended unsaved deletion appeared after that editor launch. Undo restored
the clean scene, and a controlled Play-button retry did not reproduce it. This
remains an open input-provenance investigation in `tofix.md`, not a fixed bug.

The maintained terrain, installation and Play-mode pages were deployed in
`20261011-terrain-surface-materials`. Server-side Composer checks and all 105 Twig
templates passed, production cache warmup succeeded, the health endpoint returned
`status: ok`, and the live terrain/build pages were checked for the new content.
No package versions changed in this engine-only integration.
