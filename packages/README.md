# Demi gameplay packages

These are optional Lua packages, not engine singletons. They depend only on
public Demi APIs or explicitly declared packages and keep game policy in Lua.

| Package | Responsibility |
|---|---|
| `demi.gameplay.core` | deterministic instance-owned event queues |
| `demi.gameplay.health` | health, damage, invulnerability, defeat events |
| `demi.gameplay.projectiles` | weapon timing, hit-scan/swept shots, pooling |
| `demi.gameplay.interactions` | deterministic interactions and pickups |
| `demi.gameplay.traversal` | checkpoints, entrances, respawn data |
| `demi.gameplay.camera` | follow, bounds, zones, shake, look-ahead |
| `demi.gameplay.inventory` | stacks and equipment state |
| `demi.gameplay.encounters` | waves, spawn failures, objectives |
| `demi.gameplay.controllers` | platform/top-down/click/isometric intents |
| `demi.network.lobby` | optional contract-backed lobby/ready/team/map state |

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
