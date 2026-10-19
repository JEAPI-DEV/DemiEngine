"""Author continuous native mountains around Amber Pass's playable valley.

The road corridor is exactly flat before the original sculpt strokes. Prop
placement samples the same two-metre grid triangles as TerrainSurface.cpp.
Outside that protected corridor, use native Terrain queries rather than this
limited authoring sampler: mountain noise remains owned by the engine.
"""
import math

WORLD_ORIGIN = (-360.0, 0.0, -200.0)
WORLD_SIZE = (720.0, 720.0)
RESOLUTION = (360, 360)
CORRIDOR_HALF_WIDTH = 45.0
CORRIDOR_FALLOFF = 180.0
TERRAIN_COMPONENT_SETTINGS = {
    'lod_error_pixels': 1.5,
    'surface_blending': True,
}
HILLS = (
    (-42, 45, 43, 6),
    (36, 93, 48, 8),
    (-48, 148, 48, 12),
    (44, 184, 52, 16),
)


def trail_x(z):
    return 7 * math.sin(z / 28)


def local_position(x, z):
    return [x - WORLD_ORIGIN[0], z - WORLD_ORIGIN[2]]


def sculpt_height(x, z):
    height = 0.0
    for center_x, center_z, radius, amount in HILLS:
        distance = math.hypot(x - center_x, z - center_z)
        weight = max(0.0, 1.0 - distance / radius)
        height += amount * weight * weight
    return height


def terrain_height(x, z):
    """Sample the native grid's triangle surface within the flat corridor.

    Reject mountain placements rather than silently returning a valley height.
    Existing rock/tree placements stay within 40m of the trail and remain valid.
    """
    if not (WORLD_ORIGIN[0] <= x < WORLD_ORIGIN[0] + WORLD_SIZE[0]):
        raise ValueError('Outside authored terrain X bounds')
    if not (WORLD_ORIGIN[2] <= z < WORLD_ORIGIN[2] + WORLD_SIZE[1]):
        raise ValueError('Outside authored terrain Z bounds')
    cell_size_x = WORLD_SIZE[0] / RESOLUTION[0]
    cell_size_z = WORLD_SIZE[1] / RESOLUTION[1]
    grid_x = (x - WORLD_ORIGIN[0]) / cell_size_x
    grid_z = (z - WORLD_ORIGIN[2]) / cell_size_z
    cell_x = math.floor(grid_x)
    cell_z = math.floor(grid_z)
    minimum_x = WORLD_ORIGIN[0] + cell_x * cell_size_x
    minimum_z = WORLD_ORIGIN[2] + cell_z * cell_size_z
    corners = (
        (minimum_x, minimum_z),
        (minimum_x + cell_size_x, minimum_z),
        (minimum_x, minimum_z + cell_size_z),
        (minimum_x + cell_size_x, minimum_z + cell_size_z),
    )
    # Path points are 10m apart; the 0.2m guard exceeds their sine interpolation
    # error. Each containing triangle must remain inside the zero-noise region.
    for corner_x, corner_z in corners:
        if abs(corner_x - trail_x(corner_z)) >= CORRIDOR_HALF_WIDTH - .2:
            raise ValueError('Outside the exact valley sampler; use native Terrain height queries')
    a, b, c, d = (sculpt_height(*corner) for corner in corners)
    u = grid_x - cell_x
    v = grid_z - cell_z
    if u + v <= 1:
        return a + (b - a) * u + (c - a) * v
    return d + (c - d) * (1 - u) + (b - d) * (1 - v)


def graph_node(identifier, kind, x, y, parameters=None):
    node = {'id': identifier, 'type': kind, 'position': [x, y]}
    if parameters is not None:
        node['parameters'] = parameters
    return node


def graph_link(identifier, source, destination, destination_port, source_port='field'):
    return {
        'id': identifier,
        'from': {'node': source, 'port': source_port},
        'to': {'node': destination, 'port': destination_port},
    }


def make_recipe():
    corridor = [local_position(trail_x(z), z) for z in range(-210, 531, 10)]
    regions = [
        {'biome': 'trail', 'center': local_position(trail_x(z), z),
         'radius': 3.7, 'strength': 1, 'falloff': .6}
        for z in range(-25, 194, 2)
    ]
    edits = [
        {'type': 'raise', 'center': local_position(x, z), 'radius': radius,
         'amount': amount, 'strength': 1, 'falloff': 2}
        for x, z, radius, amount in HILLS
    ]
    biome_materials = {
        'grass': 'asset://amber_pass/materials/grass',
        'trail': 'asset://amber_pass/materials/trail',
        'rock': 'asset://amber_pass/materials/mountain_rock',
    }
    return {
        'format_version': 1,
        'size': list(WORLD_SIZE),
        'resolution': list(RESOLUTION),
        'seed': 731,
        'default_biome': 'grass',
        'default_landform': 'meadow',
        'landforms': {'meadow': {'base_height': 0, 'height_variation': 0, 'feature_size': 50}},
        'biomes': {
            name: {'color': [1, 1, 1, 1], 'material': material, 'allow_snow': False}
            for name, material in biome_materials.items()
        },
        'rules': [
            {'id': 'rocky_highland', 'biome': 'rock', 'priority': 60,
             'elevation': [18, 500], 'blend': 5},
            {'id': 'rocky_slopes', 'biome': 'rock', 'priority': 80,
             'slope': [28, 90], 'blend': 5},
        ],
        'regions': regions,
        'edits': edits,
        'graph': {
            'format_version': 1,
            'output': 'output',
            'nodes': [
                # A signed noise sample stays in [-1,1], so these settings
                # bound the backdrop to 0..120m before the broad valley blend.
                # The broad shoulder blend avoids a wall at the valley boundary.
                graph_node('mountains', 'noise', 0, 0, {
                    'profile': 'ridged', 'base_height': 60, 'height_variation': 60,
                    'feature_size': 130, 'roughness': .34, 'octaves': 4,
                    'stretch': 1.35, 'heading': 65, 'warp_amplitude': 22,
                    'warp_feature_size': 220,
                }),
                graph_node('valley_floor', 'constant', 0, 280, {'height': 0}),
                graph_node('road_corridor', 'path_distance_mask', 280, 0, {
                    'points': corridor, 'width': CORRIDOR_HALF_WIDTH,
                    'falloff': CORRIDOR_FALLOFF,
                }),
                graph_node('valley_blend', 'height_blend', 560, 160, {'amount': 1}),
                graph_node('rock_and_meadow', 'biomes', 820, 160),
                graph_node('output', 'output', 1080, 160),
            ],
            'links': [
                graph_link('mountain_mask', 'mountains', 'road_corridor', 'field'),
                graph_link('mountain_base', 'mountains', 'valley_blend', 'a'),
                graph_link('flat_floor', 'valley_floor', 'valley_blend', 'b'),
                graph_link('protected_corridor', 'road_corridor', 'valley_blend', 'mask', 'mask'),
                graph_link('surface_input', 'valley_blend', 'rock_and_meadow', 'field'),
                graph_link('surface_output', 'rock_and_meadow', 'output', 'field'),
            ],
        },
    }


def write_terrain(project, write_json, manifest):
    """Use the demo's source-formatting and shared manifest authoring helpers."""
    source = project / 'assets/terrain/valley.terrain.json'
    recipe = make_recipe()
    write_json(source, {
        'format_version': 1,
        'id': 'asset://amber_pass/valley',
        'name': 'Amber Pass continuous rocky mountains and meadow corridor',
        'recipe': recipe,
    })
    dependencies = [biome['material'] for biome in recipe['biomes'].values()]
    manifest(source, 'valley', 'Terrain', 'terrain_heightfield', dependencies=dependencies)
