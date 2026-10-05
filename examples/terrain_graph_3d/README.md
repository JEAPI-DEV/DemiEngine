# Terrain graph authoring

Open with `demi editor --project examples/terrain_graph_3d`.
Select the terrain owner in the scene, then choose **Open Terrain Graph**
in its Inspector. Generation settings and connected modules are edited there.
The scene contains only an asset
reference and its placement transform.
The 128-by-128 world-unit landscape uses a 128-by-128-cell grid. Three seeded
noise sources form mountains, foothills and lowlands. Elevation masks soften
their transitions; drainage feeds a 24-iteration erosion pass. A finite lake
and its river outlet carve the basin before biome rules assign grass, exposed
rock, scree, lowland sediment and snow. Moving nodes changes layout only.

Follow the graph from left to right. The river receives both the lake's field
and its water data, and Terrain Output receives both the final biome field
and the combined water data. This keeps both authored bodies in the cooked
asset. River points are terrain-local XYZ bed positions; the water plane is
authored separately at height 10. This is a finite, level-water authoring probe,
not a downhill river simulation. The sediment rule uses elevation and slope;
graph water does not yet supply shoreline-distance context to biome rules.

Use the **Modules** tab beside Inspector to drag modules onto the graph.
Connect compatible pins, change parameters, then press **Generate**.
Return to **Viewport** to inspect, paint biomes or sculpt the generated surface.
Brush controls are on the Terrain component; manual layers remain separate
from graph evaluation. **Apply changes to asset** updates the terrain asset,
not the scene. Generated previews are cached outside the source directory.
Opening `assets/terrain/landscape.terrain.json` separately is optional when
you want to work on the asset in isolation.

Cook with `demi cook --project examples/terrain_graph_3d --output build/terrain-graph-cooked`.
Run `demi run --project build/terrain-graph-cooked` to load the generated payload
without evaluating the source graph again.

The example uses biome color tints without an external asset palette or proxy
vegetation. Terrain PBR material blending and visible water rendering are not
implemented: the lake and river currently expose their carved ground, while
water surface/depth data is retained for later rendering integration. The graph
demonstrates generation and editing, not finished landscape graphics.
