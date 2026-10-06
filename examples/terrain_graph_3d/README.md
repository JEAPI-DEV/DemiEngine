# Terrain graph authoring

Open with `demi editor --project examples/terrain_graph_3d`.
Select the terrain owner in the scene, then choose **Open Terrain Graph**
in its Inspector. Generation settings and connected modules are edited there.
The scene contains only an asset
reference and its placement transform.
The 2000-by-2000 world-unit landscape uses a 500-by-500-cell grid. Three seeded
noise sources form mountains, foothills and lowlands. Elevation masks soften
their transitions; drainage feeds a 24-iteration erosion pass. A finite lake
and its river outlet carve the basin before biome rules assign grass, exposed
rock, scree, lowland sediment and snow. Moving nodes changes layout only.

Follow the graph from left to right. The river receives both the lake's field
and its water data, and Terrain Output receives both the final biome field
and the combined water data. This keeps both authored bodies in the cooked
asset. River points are terrain-local XYZ bed positions; the water plane is
authored separately from the river bed. Both bodies use a fixed water level;
downhill river flow is not simulated. Water-body coordinates are terrain-local.
The sediment rule uses elevation and slope;
graph water does not yet supply shoreline-distance context to biome rules.

Drag cards from **Terrain Nodes** onto the graph.
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
vegetation. Terrain PBR material blending and advanced water shading are not
implemented. The connected lake and river outputs display native transparent
water, including when loaded from cooked data. Reflection/refraction, waves,
foam and swimming/buoyancy remain pending. Water nodes expose shallow/deep RGBA,
absorption distance and roughness; shore geometry is clipped to terrain triangles.
An uncontained bounded lake warns after Generate: lower the level, enlarge its
boundary or sculpt banks. The engine does not simulate spilling or auto-clamp
authored levels.

## Water mechanics in Lua

Use `require("demi.terrain.water")` for world-space water queries and point
immersion trackers. `sample({x, y, z})` returns the body, surface, depth and
`underwater` state. A tracker returns ordered `enter`, `exit` and `stay` records
each time its `update` method is called.

Add the **Water Sensor** script component from `scripts/water_sensor.lua` to an
entity to emit `water_enter`, `water_exit` and `water_stay` through `demi.events`.
The payload contains `entity_id` and `water`. Set `offset_y` to sample feet or a
head instead of the entity origin; disable `emit_stay` if only transitions are
needed. The sensor follows parented transforms. It is optional and is not added
to the scene automatically. Swimming, splashes and damage are game-defined.
