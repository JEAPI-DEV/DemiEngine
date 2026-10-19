# Water Interaction 3D

A small terrain lake fed by a sloping river, with three dynamic bodies.
Yellow opts into buoyancy with ordinary gravity. Red uses package swimming with authored gravity
disabled. A non-rendered dry control sits outside water with gravity disabled
and receives no water force. It remains identifiable in the hierarchy as
“Dry control (automated test)”. Movement uses native forces and contacts.

Install local package dependencies, then validate and run using the native CLI:

```sh
demi package install --project examples/water_interaction_3d
demi validate examples/water_interaction_3d
demi test linux --project examples/water_interaction_3d
```

The deterministic E2E probe waits five seconds and checks floating, swimming,
vertical gravity policy and the dry control. The package's physics approximation
and configuration are documented in `packages/sources/demi.gameplay.water/README.md`.
Water shading ripples do not displace the gameplay surface. This scene tests
integration and basic contacts, not boat hull simulation or a full character rig.

The inlet descends through an upstream channel and meets lake level at the
shore. Carving only lowers ground; it does not construct retaining banks.
Generation warns about low lateral banks. A wet-bank biome, outdoor fill light
and subdued lake foam keep shore contact readable from low viewing angles.
