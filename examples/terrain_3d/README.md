# Terrain authoring

From the repository root:

```sh
demi editor --project examples/terrain_3d
demi run --project examples/terrain_3d
```

Select **Paint and sculpt this terrain** in the hierarchy. Its Terrain 3D
Inspector provides draft generation settings and Generate/Regenerate. Change
the seed, regenerate, and compare the landscape: the flattened building site
stays flat, and the independent building is not moved or removed.
Press **F** over the viewport to frame the selected terrain. Generated chunks
stay out of the authored hierarchy; select and edit the terrain owner.

Choose a viewport brush to raise/lower, flatten, smooth, paint a biome or protect
an area. Release a stroke to apply it; Esc cancels. Use normal Undo/Redo and Save.
Generate or discard pending settings before painting. Protection snapshots keep
their original grid; changing size/resolution requires an explicit edit decision.

Saved source contains only the recipe, region strokes and manual edits.
Generated mesh/collider chunks are native runtime data, not authored assets.
Biome tints are discrete per cell; heights blend. Material blending, foliage,
cooked caches, dirty-region updates, LOD and streaming remain follow-up work.

See the maintained [terrain guide](https://demiengine.de/docs/terrain) and
[implementation plan](../../terrain-plan.md).
