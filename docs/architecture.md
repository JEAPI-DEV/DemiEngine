# Architecture

DemiEngine is organized around a small C++20 core, deterministic project data, and a CLI/editor pair that operate on the same runtime systems.

## Runtime Direction

- SDL3 owns Linux windows and input.
- bgfx owns GPU backend abstraction.
- EnTT owns entities and components.
- Lua 5.4 plus sol2 owns gameplay scripting.
- The current phase uses Lua 5.4 directly for lifecycle callbacks until the sol2 binding layer is introduced.
- Box2D owns 2D collision and physics.
- miniaudio owns audio playback.
- FFmpeg owns video/cutscene decoding.
- ENet is optional for networking.

## AI-Friendly Constraints

- Source data is deterministic text.
- All source formats include `format_version`.
- Validation is available through `demi validate`.
- Diagnostics include severity, code, message, and location when available.
- Editor functionality should not bypass schema validation.
