# Terrain authoring

From the repository root:

```sh
demi editor --project examples/terrain_3d
demi run --project examples/terrain_3d
```

Select **Paint and sculpt this terrain** in the hierarchy. Its Terrain 3D
Inspector provides brush tools and opens the Terrain Graph, which contains
draft generation settings and Generate/Regenerate. Change
the seed, regenerate, and compare the landscape: the flattened building site
stays flat, and the independent building is not moved or removed.
Press **F** over the viewport to frame the selected terrain. Generated chunks
stay out of the authored hierarchy; select and edit the terrain owner.

Choose a viewport brush to raise/lower, flatten, smooth, paint a biome or protect
an area, or paint scatter exclusions. Release a stroke to apply it; Esc cancels.
Use normal Undo/Redo, then **Apply changes to asset** to save the shared source.
Generate or discard pending settings before painting. Protection snapshots keep
their original grid; changing size/resolution requires an explicit edit decision.

The scene stores a Terrain asset reference. The asset source contains the
recipe, region strokes and manual edits.
Generated mesh/collider chunks are native runtime data, not authored assets.
Biome tints are discrete triangle groups; heights blend. Local strokes update
dirty chunks, and cooked builds load prepared terrain without regenerating it.
The sample now binds ordinary Material assets to its biomes. Choose a render
material and texture scale in Terrain Graph biome properties; a scale of 0.5
repeats albedo every two terrain-local units. Use Repeat wrapping on its texture.
Material and scale changes preserve heights and collision. These prototype
materials are not production landscape art or blended terrain PBR layers.
Blended materials, visible water, production foliage, integrated LOD and
large-world streaming remain follow-up work. This example uses a conventional
recipe (no connected graph) and prototype geometry for its scattered palette.

See the maintained [terrain guide](https://demiengine.de/docs/terrain) and
[implementation plan](../../terrain-plan.md).
