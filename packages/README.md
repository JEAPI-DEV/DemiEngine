# Demi packages

This page describes the package sources in this checkout. The
[hosted catalog](https://demiengine.de/packages/) lists published releases;
development sources may differ. Publishing requires registry credentials.

Browse the free package catalog at https://demiengine.de/packages/.
For this checkout's package versions, run
`demi package add demi.ui.localization@1.0.0 --registry /path/to/DemiEngine/packages`
inside a project directory. Renamed development packages are not published to the
hosted catalog automatically.
Package commands find `./demi.project.json` when `--project` is omitted.
Registry selection is `--registry`, then nonempty `DEMI_PACKAGE_REGISTRY`,
then the project's `package_registry`, then `https://demiengine.de`.
Locked installs retain the lockfile's registry unless explicitly overridden.
The local source registry below remains available for
engine development. Store application and publishing instructions live in
`tools/package-store/README.md`.

Gameplay packages are optional Lua packages, not engine singletons. They depend only on
public Demi APIs or explicitly declared packages and keep game policy in Lua.

## Choosing a package

Start with the feature you need. Health tracks damage and defeat; it does not
choose scoring or loot rules. Controllers produce movement intentions, while
your game chooses animations and physical movement. Destruction supplies weapons
and streaming policy around the engine's native fracture components.

Native services such as `demi.input` and `demi.physics.rigidbody3d` already ship
with the engine and do not need a package install. Package names identify an
installable unit; `public_modules` in its manifest lists the Lua imports it
exports. See [engine concepts](../docs/engine-concepts.md) for the distinction.

| Package | Responsibility |
|---|---|
| `kenney.textures.prototype` | 78 CC0 prototype textures with ready-to-use asset IDs (no Lua required) |
| [`demi.gameplay.events`](sources/demi.gameplay.events/README.md) | Queued Lua events with ordered listeners and unsubscribe support; formerly `demi.gameplay.core` |
| `demi.gameplay.health` | health, damage, invulnerability, defeat events |
| `demi.gameplay.projectiles` | weapon timing, hit-scan/swept shots, pooling |
| `demi.gameplay.destruction` | timed 3D hammer contacts, swept rockets and native-impact adapter |
| `demi.gameplay.interactions` | deterministic interactions and pickups |
| `demi.gameplay.checkpoints` | checkpoints, entrances, respawn data |
| `demi.gameplay.camera` | follow, bounds, zones, shake, look-ahead |
| `demi.gameplay.inventory` | stacks and equipment state |
| `demi.gameplay.encounters` | waves, spawn failures, objectives |
| `demi.gameplay.controllers` | platform/top-down/click/isometric intents |
| `demi.gameplay.third_person` | 3D orbit camera, movement/rolls, melee attack phases |
| `demi.network.lobby` | optional contract-backed lobby/ready/team/map state |
| `demi.ui.localization` | cached YAML languages applied to native HUD variables |

Projects declare constraints in `demi.project.json`:

```json
{
  "package_registry": "../../packages",
  "packages": {
    "demi.gameplay.health": "^1.0.0"
  }
}
```

Then resolve and install:

```sh
demi package install --project path/to/demi.project.json
demi package install --locked --offline --project path/to/demi.project.json
demi package test packages/sources/demi.gameplay.health
```

New projects include `.demi/packages` in their LuaLS workspace library, so
installed public modules are indexed without copying them into game scripts.

`demi.packages.lock.json` is committed. `.demi/packages/` and the verified
download cache are derived state. The runtime only loads installed package
modules; it never contacts a registry.
