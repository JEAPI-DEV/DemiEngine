# Rocky valley terrain probe

Open with `demi editor --project examples/rocky_valley_3d` or run with
`demi run --project examples/rocky_valley_3d`.
Select **terrain**, then **Open Terrain Graph** to inspect the connected stages.
This separate example preserves the sculpted `terrain_graph_3d` source.

Seed **1704** generates a 2,000 × 2,000 world-unit landscape on the same
500 × 500-cell grid as the earlier probe: four world units per cell.
The mountain source uses ridged noise centred at 250 units with 230 units
of signed variation, 420-unit features, five octaves and roughness 0.34.
A stretch of three along an 80-degree heading forms long connected masses
rather than a repeated field of small peaks. An 80-unit domain warp at a
700-unit feature scale bends their alignment.

The `valley_corridor` points are terrain-local `[x,z]` positions. The mask
has a **90-unit half width**, so its fully weighted floor is 180 units wide;
a further 220-unit smooth falloff forms its shoulders. `valley_blend`
blends the mountain source toward a gently varying 18-unit floor. Moving
these points changes the valley route without changing the seed or mountain
source. Duplicate points are supported. The finite polyline has rounded
end caps; extend its endpoints outside the field when a corridor should
continue beyond the authored boundary.

Drainage feeds a 16-iteration erosion pass, then a separate river node carves
a 14-unit channel and publishes water at elevation 19. River points carry
terrain-local XYZ bed positions at elevation 15. The water surface uses a
fixed level; this is not a downhill flow simulation. Slope and elevation
rules assign exposed rock, scree and high snow after the final geometry.
The game camera uses a 5,000-unit far clip to include the whole landscape.
Its elevated position looks down the curved valley so the floor, river and
ridge tops are visible together.

The surface palette uses the Poly Haven Terrain Materials package's ordinary PBR
materials with white biome tints. Rock/cliff roles select the pale gray fractured
Marble Cliff 05 scan at its physical 20-metre tile width; Rock Face 03 remains
available in the package as a warm brown alternative. Changing the material does
not change terrain geometry or the 500 × 500-cell resolution.

Compatible GPU terrain blends neighboring mapped samples and supports triplanar
albedo, normals and roughness. The ordinary mesh fallback retains discrete
material boundaries. Source maps provide surface shading, not geometric cracks,
vegetation or image-based lighting. Shadows remain enabled to expose receiver
quality; the earlier prominent cliff shadow grid has been corrected.
Generated and cooked output stays outside the authored asset sources.
