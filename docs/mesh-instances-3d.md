# Static mesh instances

`MeshInstances3D` renders local copies of the owner's `MeshRenderer` without
creating an entity for each copy. It works with shared static models, primitives
and inline static geometry. Each copy has a stable label and a local transform:

```json
"MeshInstances3D": {
  "transforms": {
    "left": { "position": [-1, 0, 0] },
    "right": { "position": [1, 0, 0], "rotation": [0, 0.785398, 0] }
  }
}
```

Position and rotation default to zero; rotations use radians, as in Transform3D.
Scale defaults to one. Empty transforms
render no copies. Invalid vectors, non-finite values, zero scales and unknown
transform fields are rejected. The owner supplies mesh, material, texture and
LOD settings. Camera culling evaluates each transformed copy. Compatible copies
use the renderer's existing instance batching; custom materials can still require
separate draws.

These are visual instances, not independent gameplay objects. A collider or
script on the owner is not duplicated. Use ordinary entities or prefabs for
independent physics, scripts or skeletal animation. `AnimationPlayer3D` cannot
currently be combined with this component.

The Inspector edits the transform map through the standard structured-value
editor. Picking a visible copy selects its owner. Copies follow the owner's
transform hierarchy, using the same transform composition as scene entities.

## Intact custom-model masonry

The fracture compiler groups a custom masonry region by model asset. One live
renderer per distinct model stores the cell transforms. Brick shard entities
remain dormant until their region splits across physical bodies. Activation
removes the intact batches in the same transaction that publishes the shards;
other regions and prefab instances retain their intact batches.

Original model geometry and texture references remain in use. This does not
replace detailed bricks with a flat wall or reduce their polygon count. The
support graph, collision parts and cold shard descriptions remain resident;
lower entity counts do not establish a large-map frame-rate guarantee.
