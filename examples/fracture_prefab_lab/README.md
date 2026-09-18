# Fracture prefab authoring lab

```sh
demi run --project examples/fracture_prefab_lab
```

LMB applies a localized directional strike; RMB applies a larger radial blast;
R resets. Both walls instantiate the same prefab but have independent damage
state. Nearby structures share the explosion's energy/impulse budget. Loose
pieces can be struck again. Forces and fragment selection are engine-owned.
Strikes accumulate damage; a low-energy hit can weaken connections without
immediately separating a fragment.

Edit `prefabs/wall.prefab.json` as an ordinary multi-object prefab. Its wall root
has `Destructible3D`; nested concrete and reinforcement meshes have `Fracture3D`
for fragment counts, connection health and foundation levels. The engine
generates the shard meshes, cut surfaces, colliders, bonds and visual mappings.
The single prefab contains no manual shard vertices or bond lists.

In the editor, use **Add Component → Destructible 3D / Fracture 3D** and edit the
settings in the Inspector. Source objects remain editable; Play loads generated
geometry and cooking bakes it. No separate recipe prefab is needed.

This demonstrates convex authoring and the first spatial impact API, not finished
hammer/rocket gameplay or structural stress. See [supported inputs and limits](../../docs/fracture-authoring.md)
and [impact semantics](../../docs/3d-spatial-impacts.md).
