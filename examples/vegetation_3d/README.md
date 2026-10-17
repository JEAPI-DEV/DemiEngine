# Woodland vegetation qualification

This independent 96 × 96 m scene exercises the original CC0
`demi.terrain.vegetation` package with a contained lake, a sloped upland, automatic
oak/pine forest, dense grass, shoreline reeds and clustered boulders.

```sh
./build/linux-debug/demi package install --project examples/vegetation_3d/demi.project.json
./build/linux-debug/demi validate examples/vegetation_3d
./build/linux-release/demi run --project examples/vegetation_3d/demi.project.json
```

The road reservation at terrain-local x=60 is painted exclusion, represented
visually by a short flat road marker on its level southern section. The building
at [78,72] owns a circular exclusion clearing. Their exclusions intentionally
use the same shared authored mask contract, so neither needs gameplay code.
The connected lake coverage blocks all submerged placements before spacing;
reeds occupy the dry 2.5 m bank band, trees stay at least 4–5 m from water,
and steep uplands reject grass/trees while allowing rocks. The upland is
procedural and the road's northern reservation is not a draped road mesh.

The graph keeps terrain, connected water, biome decisions and scattered
instances as explicit branches. Original indexed source meshes and ordinary
Materials come entirely from the package. Grass/reeds have no physics bodies;
trees have trunk capsules, rocks have conservative box colliders. Every mesh
has genuine reduced medium/low alternatives and finite distance culling.

Use the Release runtime/editor when measuring. Record build, camera, palette
densities, accepted placement counts, frame-time percentiles, CPU render cost,
draw/instance counts and GPU residency. Compare the same seed/camera with
`grass_density`, `tree_density`, `rock_density` or `art_density` set to zero.
The model-only low/medium variants can be compared by selecting the corresponding
asset in the normal MeshRenderer fields. This example alone is not a measured
performance result; qualification evidence belongs in `docs/`.

Changing the seed changes selected sites and variation. Changing a placement
rule ID changes its identities; surviving sites keep their identity when only
the seed or density changes.
Density changes should retain surviving sites. Edit a local road exclusion and
regenerate to check that unrelated forests keep their transforms. Use the
terrain Inspector's **Generated vegetation and detail → Convert** command to preserve it as an
authored object, then regenerate and check that it is not duplicated.
