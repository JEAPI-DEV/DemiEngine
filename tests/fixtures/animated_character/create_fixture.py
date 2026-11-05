"""Create the original Z-up skinned mannequin fixture in Blender.

Run with Blender's Python API, or: blender --background --python create_fixture.py
Only the new scene is exported; existing scenes are left untouched.
"""
from pathlib import Path
import math
import bpy


def create_fixture(output_directory):
    scene = bpy.data.scenes.new("Animation regression mannequin")
    bpy.context.window.scene = scene
    scene.render.fps = 24
    scene.frame_start = 1
    scene.frame_end = 25

    rig_data = bpy.data.armatures.new("Mannequin skeleton")
    rig = bpy.data.objects.new("Mannequin rig", rig_data)
    scene.collection.objects.link(rig)
    bpy.context.view_layer.objects.active = rig
    rig.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")
    bones = {
        "hips": ((0, 0, 0.9), (0, 0, 1.1), None),
        "torso": ((0, 0, 1.1), (0, 0, 1.5), "hips"),
        "head": ((0, 0, 1.5), (0, 0, 1.85), "torso"),
        "arm_left": ((-0.3, 0, 1.45), (-0.3, 0, 0.95), "torso"),
        "arm_right": ((0.3, 0, 1.45), (0.3, 0, 0.95), "torso"),
        "leg_left": ((-0.14, 0, 0.9), (-0.14, 0, 0.1), "hips"),
        "leg_right": ((0.14, 0, 0.9), (0.14, 0, 0.1), "hips"),
    }
    for name, (head, tail, parent) in bones.items():
        bone = rig_data.edit_bones.new(name)
        bone.head = head
        bone.tail = tail
        if parent:
            bone.parent = rig_data.edit_bones[parent]
    bpy.ops.object.mode_set(mode="OBJECT")

    vertices, faces, groups = [], [], {}
    parts = [
        ("hips", (0, 0, 1.0), (0.42, 0.25, 0.2)),
        ("torso", (0, 0, 1.3), (0.48, 0.28, 0.4)),
        ("head", (0, -0.015, 1.7), (0.3, 0.3, 0.35)),
        ("arm_left", (-0.34, 0, 1.17), (0.16, 0.18, 0.5)),
        ("arm_right", (0.34, 0, 1.17), (0.16, 0.18, 0.5)),
        ("leg_left", (-0.14, 0, 0.5), (0.2, 0.22, 0.78)),
        ("leg_right", (0.14, 0, 0.5), (0.2, 0.22, 0.78)),
    ]
    corners = [(-1,-1,-1), (1,-1,-1), (1,1,-1), (-1,1,-1),
               (-1,-1,1), (1,-1,1), (1,1,1), (-1,1,1)]
    cube_faces = [(0,3,2,1), (4,5,6,7), (0,1,5,4),
                  (1,2,6,5), (2,3,7,6), (3,0,4,7)]
    for bone, center, size in parts:
        offset = len(vertices)
        vertices.extend(tuple(center[i] + corner[i] * size[i] / 2
                              for i in range(3)) for corner in corners)
        faces.extend(tuple(offset + i for i in face) for face in cube_faces)
        groups[bone] = list(range(offset, offset + 8))
    mesh = bpy.data.meshes.new("Original block mannequin")
    mesh.from_pydata(vertices, [], faces)
    mesh.update()
    body = bpy.data.objects.new("Mannequin", mesh)
    scene.collection.objects.link(body)
    for name, indices in groups.items():
        body.vertex_groups.new(name=name).add(indices, 1.0, "REPLACE")
    body.modifiers.new("Skin", "ARMATURE").object = rig
    body.parent = rig
    material = bpy.data.materials.new("Plain blue gray")
    material.diffuse_color = (0.3, 0.42, 0.55, 1)
    mesh.materials.append(material)

    clips = {
        "Idle": {"head": (0, 0.12, 0), "torso": (0.025, 0, 0)},
        "CombatRun": {"leg_left": (0.65, 0, 0), "leg_right": (-0.65, 0, 0),
                      "arm_left": (-0.5, 0, 0), "arm_right": (0.5, 0, 0)},
        "DodgeRoll": {"hips": (math.tau, 0, 0)},
        "SwordAttack": {"arm_right": (-1.5, 0, 0.5), "torso": (0, 0.3, 0)},
        "ShieldBlock": {"arm_left": (-1.2, 0, -0.4)},
    }
    rig.animation_data_create()
    for name, rotations in clips.items():
        action = bpy.data.actions.new(name)
        action.use_fake_user = True
        rig.animation_data.action = action
        for frame, weight in ((1, 0), (13, 0.5 if name == "DodgeRoll" else 1),
                              (25, 1 if name == "DodgeRoll" else 0)):
            for bone in rig.pose.bones:
                bone.rotation_mode = "XYZ"
                target = rotations.get(bone.name, (0, 0, 0))
                bone.rotation_euler = tuple(value * weight for value in target)
                bone.keyframe_insert("rotation_euler", frame=frame)
    rig.animation_data.action = None
    for bone in rig.pose.bones:
        bone.rotation_euler = (0, 0, 0)
    scene.frame_set(1)
    output = Path(output_directory) / "mannequin.glb"
    bpy.ops.export_scene.gltf(filepath=str(output), export_format="GLB",
                             use_active_scene=True, export_yup=False,
                             export_animation_mode="ACTIONS", export_force_sampling=True)
    print(f"Exported original mannequin: {output}")


if __name__ == "__main__":
    create_fixture(Path(__file__).resolve().parent)
