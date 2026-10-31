# Woodland tree impostor

Six triangles and a three-view albedo atlas baked from the same CC0 woodland
source as the near and medium models. All tiers share one origin and scale.
Runtime lighting is applied once; directional light is not baked into the atlas.
Source: packages/sources/demi.environment.woodland (see its README and LICENSE).

Reproduce with scripts/art/prepare_woodland_tree.py in a separate Blender
background process, exporting to a temporary directory. Import the resulting
near, mid and distant glTF files with the normal Demi asset importer and the
environment preset. Original package assets and interactive Blender sessions
are left untouched. These are visual LODs, not collider or gameplay replacements.
