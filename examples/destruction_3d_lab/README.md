# 3D Destruction Lab — localized physical splits

```sh
demi run --project examples/destruction_3d_lab
```

The arch starts as one anchored static compound with three convex parts and a
real opening. Click a part twice to break its incident bonds, or press Space /
Strike Beam to detach and strike the beam. R / Reset restores the assembly.
Anchored pillars stay fixed; the detached beam moves, rotates and falls using
Jolt collision and gravity. The strike impulse is Lua gameplay; damage state,
splitting, mass/motion inheritance and visual reparenting are engine-native.
The status label/console reports hits, committed body counts and failures.

The entire scene is authored in `scenes/main.scene.json`. The renderer-only
children initially follow `arch`; collision geometry and local part IDs live in
`assets/colliders/arch/arch.collider.json`. Reimport its manifest after editing
the collider file. Lua handles controls and the strike impulse, not construction.
The collider source also contains optional `fracture.bonds` and
`fracture.anchors`, editable by stable part ID. The root's `Destructible3D.parts`
map links those IDs to renderer children.

This is not the completed destruction demo: hammer/rocket interactions, spatial
blast falloff, fair scheduling, performance qualification and the steel door
remain Milestone 3 work. The primitive arch is an engine probe, not final art.
