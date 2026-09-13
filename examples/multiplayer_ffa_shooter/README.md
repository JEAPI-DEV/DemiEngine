# Multiplayer FFA Shooter

A shared Linux and Android top-down shooter example. One player hosts the
authoritative match; the others join its LAN address. Movement uses sequenced
owner input intents, immediate local prediction, authoritative fixed-tick
snapshots, correction/replay, and bounded interpolation for other players.
Player identity, lag-compensated host-validated hits, health, respawning, and
scoring all use the game-facing `NetworkSession` API.

## Linux

```sh
cmake --build --preset linux-debug
./build/linux-debug/demi run --project examples/multiplayer_ffa_shooter/demi.project.json
```

Open a second instance, leave `127.0.0.1` in the address field, and choose
`JOIN`. Use the host machine's LAN address when the players are on different
devices. Linux controls are WASD/arrows to move and mouse/Space to fire.

## Android

```sh
./build/linux-debug/demi build apk \
  --project examples/multiplayer_ffa_shooter/demi.project.json
```

Install the generated APK, enter the Linux host's LAN address, and tap `JOIN`.
The safe-area-aware virtual stick and `FIRE` button feed the same `move_x`,
`move_y`, and `fire` actions used by keyboard and gamepad controls. Android and
Linux must be on a network where UDP port `39420` is reachable.

`OFFLINE PRACTICE` starts the arena without a network connection.

## Authority and latency

Clients never replicate transforms directly. `move_input` is an
owner-to-server `owned_entity` message validated by the network contract. The
server applies the same deterministic controller function used for client
prediction, acknowledges evaluated or discarded sequences, and publishes
authoritative state. Missing history, reconnects, ownership changes, and
teleports snap safely instead of replaying incomplete state.

The host retains only 32 selected player-circle snapshots and rewinds queries
by at most 12 fixed ticks for hitscan validation. The live Box2D world is never
rewound. The `demi-network-prediction-tests` target drives this controller
through deterministic latency, jitter, loss, duplication, reordering, and tick
gaps and verifies the authority against the accepted input log.
