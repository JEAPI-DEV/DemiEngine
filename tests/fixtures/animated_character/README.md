# Animated character import regression fixture

`mannequin.glb` is an original block mannequin created in Blender for this
repository. Its geometry, seven-bone rig, flat blue-gray material, and five
keyframed actions are constructed entirely by `create_fixture.py`. No imported
models, textures, motion capture, or third-party artwork are used. It replaces
the supplied cyberpunk character and atlas, which are no longer in this fixture.

Regenerate with Blender (created and tested with Blender 5.2.3):

```sh
blender --background --python tests/fixtures/animated_character/create_fixture.py
```

The script also runs through the Blender MCP Python tool. It creates a separate
scene and exports only that scene, without deleting existing scene content.
The generator is the editable source; no separate `.blend` file is required.
Export intentionally retains Blender's Z-up coordinates (`export_yup=False`) to
exercise the engine's `source_up: +z`, `source_forward: -y` import conversion.

`gltf_skinned_model_tests.cpp` checks upright bind bounds, finite and bounded
sampled vertices, and actual mesh deformation in Idle, CombatRun, DodgeRoll,
SwordAttack, and ShieldBlock. All actions are authored in place. This small
fixture tests import and skinning; it does not reproduce the old third-party
asset's full skeleton or validate a motion-retargeting pipeline.
