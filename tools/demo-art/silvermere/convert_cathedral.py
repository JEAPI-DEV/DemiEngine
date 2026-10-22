"""Convert the CC0 3TD cathedral subset to a meter-scale, textured glTF.

Run with Blender 5.2:
    blender -b --python tools/demo-art/silvermere/convert_cathedral.py -- \
        /path/to/FantasyRuins_Ready /path/to/cathedral.glb

The source archive and license are recorded alongside the resulting asset.
Only the authored visual mesh is imported; Torque collision meshes and its
scene-offset bookkeeping nodes are intentionally excluded.
"""

import json
from pathlib import Path
import sys
import xml.etree.ElementTree as ElementTree

import bpy
from mathutils import Vector


MATERIAL_TEXTURES = {
    "FloorBrick_color_02A": ("2td_FloorBrick_color_02A.jpg", "2td_FloorBrick_color_02_NRM.png"),
    "BrickOldRound03": ("3td_BrickOldRound_03.jpg", "3td_BrickOldRound_03_NRM.png"),
    "BrickDebris01": ("3td_BrickDebris_01.jpg", "3td_BrickDebris_01_NRM.png"),
    "BrickOldColor34": ("3td_BrickOldColor_34.jpg", "3td_BrickOldColor_34_NRM.png"),
    "WoodDebris17": ("3td_WoodDebris_17.jpg", "3td_WoodDebris_17_NRM.png"),
    "CementDirty1": ("3td_ConcreteDirty_01.jpg", "3td_ConcreteDirty_01_NRM.png"),
}
NAMESPACE = {"c": "http://www.collada.org/2005/11/COLLADASchema"}


def make_material(name, source_directory):
    material = bpy.data.materials.new(name)
    material.use_nodes = True
    shader = material.node_tree.nodes.get("Principled BSDF")
    shader.inputs["Roughness"].default_value = 0.86
    shader.inputs["Metallic"].default_value = 0.0
    diffuse_file, normal_file = MATERIAL_TEXTURES[name]
    diffuse = material.node_tree.nodes.new("ShaderNodeTexImage")
    diffuse.image = bpy.data.images.load(str(source_directory / diffuse_file))
    material.node_tree.links.new(diffuse.outputs["Color"], shader.inputs["Base Color"])
    normal_texture = material.node_tree.nodes.new("ShaderNodeTexImage")
    normal_texture.image = bpy.data.images.load(str(source_directory / normal_file))
    normal_texture.image.colorspace_settings.name = "Non-Color"
    normal_map = material.node_tree.nodes.new("ShaderNodeNormalMap")
    material.node_tree.links.new(normal_texture.outputs["Color"], normal_map.inputs["Color"])
    material.node_tree.links.new(normal_map.outputs["Normal"], shader.inputs["Normal"])
    return material


def convert(source_root, destination):
    source_directory = source_root / "game/art/shapes/Ruins/CathedralRuins_01"
    root = ElementTree.parse(source_directory / "CathedralRuins_01.dae").getroot()
    geometry = root.find(".//c:geometry[@id='geom-CathedralRuinOne300']/c:mesh", NAMESPACE)
    if geometry is None:
        raise ValueError("The archive does not contain the expected visual mesh")
    sources = {}
    for source in geometry.findall("c:source", NAMESPACE):
        values = list(map(float, source.find("c:float_array", NAMESPACE).text.split()))
        stride = int(source.find("c:technique_common/c:accessor", NAMESPACE).get("stride"))
        sources[source.get("id")] = [values[i:i + stride] for i in range(0, len(values), stride)]
    positions = sources["geom-CathedralRuinOne300-positions"]
    # Source authored in feet, with a uniform object scale in the COLLADA node.
    object_node = root.find(".//c:node[@id='node-CathedralRuinOne300']", NAMESPACE)
    object_scale = list(map(float, object_node.find("c:scale", NAMESPACE).text.split()))
    meters_per_unit = float(root.find("c:asset/c:unit", NAMESPACE).get("meter"))
    points = [Vector(tuple(value[axis] * object_scale[axis] * meters_per_unit for axis in range(3))) for value in positions]
    minimum = Vector(tuple(min(point[axis] for point in points) for axis in range(3)))
    maximum = Vector(tuple(max(point[axis] for point in points) for axis in range(3)))
    origin = Vector(((minimum.x + maximum.x) / 2, (minimum.y + maximum.y) / 2, minimum.z))
    points = [point - origin for point in points]
    material_names = list(MATERIAL_TEXTURES)
    material_symbols = {}
    for binding in object_node.findall(".//c:instance_material", NAMESPACE):
        material_symbols[binding.get("symbol")] = binding.get("target").removeprefix("#").removesuffix("-material")
    faces = []
    uv_values = []
    normal_values = []
    material_indices = []
    for triangles in geometry.findall("c:triangles", NAMESPACE):
        inputs = {item.get("semantic"): item for item in triangles.findall("c:input", NAMESPACE)}
        stride = max(int(item.get("offset")) for item in inputs.values()) + 1
        indices = list(map(int, triangles.find("c:p", NAMESPACE).text.split()))
        uv_source = sources[inputs["TEXCOORD"].get("source").removeprefix("#")]
        normal_source = sources[inputs["NORMAL"].get("source").removeprefix("#")]
        for start in range(0, len(indices), stride * 3):
            face = []
            for corner in range(3):
                entry = start + corner * stride
                face.append(indices[entry + int(inputs["VERTEX"].get("offset"))])
                uv_values.append(uv_source[indices[entry + int(inputs["TEXCOORD"].get("offset"))]][:2])
                normal_values.append(normal_source[indices[entry + int(inputs["NORMAL"].get("offset"))]])
            faces.append(face)
            material_indices.append(material_names.index(material_symbols[triangles.get("material")]))
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    mesh = bpy.data.meshes.new("3TD cathedral visual mesh")
    mesh.from_pydata(points, [], faces)
    mesh.update()
    uv_layer = mesh.uv_layers.new(name="UVMap")
    for loop, uv in zip(uv_layer.data, uv_values):
        loop.uv = uv
    mesh.normals_split_custom_set(normal_values)
    for polygon, material_index in zip(mesh.polygons, material_indices):
        polygon.material_index = material_index
        polygon.use_smooth = True
    cathedral = bpy.data.objects.new("Silvermere cathedral ruin", mesh)
    bpy.context.collection.objects.link(cathedral)
    for name in material_names:
        mesh.materials.append(make_material(name, source_directory))
    cathedral.select_set(True)
    bpy.context.view_layer.objects.active = cathedral
    destination.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.export_scene.gltf(
        filepath=str(destination),
        export_format="GLB",
        use_selection=True,
        export_yup=True,
        export_normals=True,
        export_tangents=True,
        export_image_format="AUTO",
    )
    summary = {
        "triangles": len(faces),
        "source_position_vertices": len(points),
        "materials": material_names,
        "bounds_meters_blender_z_up": {
            "min": list(minimum - origin),
            "max": list(maximum - origin),
        },
    }
    print(json.dumps(summary))


if __name__ == "__main__":
    arguments = sys.argv[sys.argv.index("--") + 1:]
    if len(arguments) != 2:
        raise SystemExit("Expected extracted FantasyRuins_Ready directory and destination GLB")
    convert(Path(arguments[0]), Path(arguments[1]))
