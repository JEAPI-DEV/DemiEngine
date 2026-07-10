# Minimal 2D Android Server

This is the dedicated Lua DTLS server project for `examples/minimal_2d_android`.

The server owns the public lobby/session and relays gameplay over ENet. Android clients connect to this server; they do not bind gameplay ports and do not use the old PHP/MySQL lobby.

## Local Emulator Test

From the repo root:

```sh
cmake --preset linux-debug
cmake --build --preset linux-debug
mkdir -p build/dtls-local
openssl req -x509 -newkey ec \
  -pkeyopt ec_paramgen_curve:prime256v1 \
  -keyout build/dtls-local/server.key \
  -out build/dtls-local/server.crt \
  -days 365 -nodes -subj "/CN=localhost" \
  -addext "subjectAltName=DNS:localhost,IP:127.0.0.1"
DEMI_DTLS_CERT="$PWD/build/dtls-local/server.crt" \
DEMI_DTLS_KEY="$PWD/build/dtls-local/server.key" \
  ./build/linux-debug/demi serve \
  --project examples/minimal_2d_android_server/demi.project.json
```

To make a portable server folder:

```sh
./build/linux-debug/demi build linux_server \
  --project examples/minimal_2d_android_server/demi.project.json
mkdir -p build/linux_server/minimal_2d_android_server/project/certs
# Install server.crt and server.key in that certs directory.
./build/linux_server/minimal_2d_android_server/serve
```

The server listens on TCP `39421` for raw TLS matchmaking and UDP `39420` for
DTLS gameplay. Both use the same certificate through mbedTLS. On the ip `127.0.0.1`.

The game protocol is implemented entirely in `scripts/server.lua`. The engine
supplies the headless Lua runtime, ENet transport through `Network`, and JSON
message helpers. Coin claims are scoped to the running server session because
procedurally generated coin IDs are reused when a new match starts.

## VPS

Create the certificate with your VPS hostname in its `subjectAltName`, install
the certificate and private key under `project/certs`, and open TCP `39421`
plus UDP `39420`. Keep `server.key` readable only by the server account:

```sh
chmod 600 project/certs/server.key
sudo ufw allow 39421/tcp
sudo ufw allow 39420/udp
```

Ship `server.crt` with the game client and configure the session:

```lua
NetworkSession.configure({
  trusted_certificate = "certs/server.crt",
  server_name = "game.example.com",
})
```

For the Android example, place the public certificate at
`examples/minimal_2d_android/certs/server.crt` and set `server_name` in
`scripts/network_lobby.lua` to the certificate hostname. Never ship
`server.key` with a client.

Then update `examples/minimal_2d_android/scripts/network_lobby.lua`:

```lua
server_host = "YOUR_VPS_IP_OR_DOMAIN"
server_port = 39420
```

The client rejects certificates that are not signed by the pinned certificate
or whose hostname does not match `server_name`.

Matchmaking messages are length-framed JSON over raw TLS, not HTTP. Creating or
joining a lobby returns a short-lived token that must be presented over DTLS
before gameplay session data is sent.

## Server Behavior

- First connected client gets sender id `host`.
- Later clients get `peer2`, `peer3`, `peer4`.
- Server sends `session_start` to every client.
- Server relays transform snapshots.
- Server stores one-time coin claims in the running session and broadcasts the winning claim.
