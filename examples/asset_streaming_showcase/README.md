# Asset Streaming Showcase

This example keeps its initial scene asset-free and manages an optional theme
pack through the public Lua `Assets` API. The group root has a transitive SVG
dependency, so a graphical run exercises bgfx-native ownership while a
headless run uses the resident-source fallback.

Run it with:

```sh
cmake --build --preset linux-debug
./build/linux-debug/demi validate examples/asset_streaming_showcase/demi.project.json
./build/linux-debug/demi run --project examples/asset_streaming_showcase/demi.project.json
```

The group loads asynchronously and its two SVG resources appear when the load
request is ready. The buttons demonstrate loading, cancelling an in-flight
request, reloading the root through the same backend path, and unloading the
group. Unload clears the HUD references before returning the
native resources. The memory display lists each resident stable ID, its owning
backend, and total resident bytes.

For watched iteration, launch with `--watch`, edit
`assets/theme/accent.svg`, then refresh its deterministic source hash:

```sh
./build/linux-debug/demi asset reimport \
  examples/asset_streaming_showcase/assets/theme/accent.asset.json
```

The watcher reloads the resident asset automatically. **Reload root** is the
manual equivalent and uses the same ownership path when watch mode is off.

The scene intentionally does not mention the theme assets. Scene-owned content
is discovered from scene references automatically; explicit groups are for
independent lifetimes such as optional themes, voice packs, or the next chapter.
