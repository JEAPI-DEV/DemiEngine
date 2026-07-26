# Multiplayer FFA Shooter

A shared Linux and Android top-down shooter example. One player hosts the
authoritative match; the others join its LAN address. Movement replication,
player identity, host-validated hits, health, respawning, and scoring all use
the game-facing `NetworkSession` API.

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
The anchored direction pad and `FIRE` button are shared with the desktop build.
Android and Linux must be on a network where UDP port `39420` is reachable.

`OFFLINE PRACTICE` starts the arena without a network connection.
