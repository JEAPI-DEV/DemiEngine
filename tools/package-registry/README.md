# Local package registry

Start the disposable development registry:

```sh
docker compose -f tools/package-registry/compose.yaml up --build -d
./build/linux-debug/demi package publish packages/sources/demi.gameplay.core \
  --registry http://localhost:8080
./build/linux-debug/demi package add demi.gameplay.core@^1.0.0 \
  --project examples/your_game/demi.project.json --registry http://localhost:8080
```

Registry versions are immutable. Reset the development data with:

```sh
curl -X DELETE http://localhost:8080/v1/test/reset
```

The reset endpoint is intentionally development-only. Do not expose this
container as a public production registry.
