# Fracture prefab authoring lab

```sh
demi run --project examples/fracture_prefab_lab
```

LMB strikes a generated shard; R resets. Both walls instantiate the same prefab
but have independent damage state. Loose pieces can be struck again.

Edit `prefabs/wall.prefab.json` as an ordinary multi-object prefab. Its wall root
has `Destructible3D`; nested concrete and reinforcement meshes have `Fracture3D`
for fragment counts, connection health and foundation levels. The engine
generates the shard meshes, cut surfaces, colliders, bonds and visual mappings.
The single prefab contains no manual shard vertices or bond lists.

In the editor, use **Add Component → Destructible 3D / Fracture 3D** and edit the
settings in the Inspector. Source objects remain editable; Play loads generated
geometry and cooking bakes it. No separate recipe prefab is needed.

This demonstrates the first convex authoring workflow, not complete spatial
weapon damage or structural stress. See [supported inputs and limits](../../docs/fracture-authoring.md).
