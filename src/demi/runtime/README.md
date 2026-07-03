# Runtime Modules

The runtime is split by engine responsibility. Public compatibility headers live
directly in `src/demi/runtime/` and forward to the implementation submodules, so
existing includes such as `demi/runtime/scripting/LuaScriptHost.h` remain stable.

- `app/`: runtime orchestration and frame loop entry points.
- `audio/`: audio device and source playback.
- `media/`: video playback and media-backed runtime resources.
- `network/`: low-level network transport and events.
- `physics/`: reusable 2D and 3D movement/collision behavior.
- `render/`: 2D and 3D renderer implementations.
- `scene/`: runtime scene/project data and loading.
- `scripting/`: Lua host, bindings, persistence, and script lifecycle.

Prefer adding reusable engine behavior to the owning module instead of hiding it
inside Lua bindings or examples. Bindings should adapt module APIs to gameplay
scripts; they should not become the only implementation of runtime behavior.
