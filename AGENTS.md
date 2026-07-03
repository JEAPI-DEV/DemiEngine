# Agent Rules For DemiEngine

This repository is designed for AI-assisted development. Keep source data deterministic, validate changes through the CLI, and avoid hidden editor-only state.

## Build And Test

- Configure: `cmake --preset linux-debug`
- Build: `cmake --build --preset linux-debug`
- Test: `ctest --preset linux-debug`
- CLI help: `./build/linux-debug/demi --help`
- Validate examples: `./build/linux-debug/demi validate examples/minimal_2d_networking/demi.project.json`
- Validate 3D example: `./build/linux-debug/demi validate examples/minimal_3d/demi.project.json`
- Headless runtime smoke: `DEMI_HEADLESS=1 ./build/linux-debug/demi run --project examples/minimal_3d/demi.project.json --max-frames 3`

## Source Vs Generated Files

- Source files are edited by humans and agents.
- Generated files belong in `build/`, `generated/`, or `examples/**/generated/`.
- Do not hand-edit generated files.
- Do not commit build outputs.
- `scripts/stubs/demi.lua` is checked-in source for the Lua stub generator in this repo; update it when public Lua APIs change.

## Examples Are Engine Probes

- Treat example bugs as evidence of missing reusable engine features, not as a reason for one-off script hacks.
- Prefer fixing `src/demi/runtime`, schemas, validation, Lua bindings, and tests when an example reveals a general capability gap.
- Keep example scripts small and idiomatic; use them to exercise engine APIs rather than duplicate engine behavior.
- After changing example data or scripts, validate the owning example project and run at least the relevant script/runtime tests.

## Data Format Rules

- Every project, scene, save, and future asset manifest must include `format_version`.
- Asset manifests use `*.asset.json` and stable `asset://` ids.
- Prefer stable IDs over positional references.
- Prefer URI-style references such as `scene://main`, `asset://textures/unit.png`, and `script://units/archer.lua`.
- Keep formatting stable so diffs are small and reviewable.
- Run `demi validate` after editing example project data.
- When adding a scene component field, update the C++ component, scene loader, schema, Lua creation/bindings if applicable, stubs if public, and tests together.
- Runtime data should be deterministic and serializable. Avoid hidden state that only exists in editor UI or Lua script globals when it belongs in scene/project data.

## C++ Rules

- Use C++20.
- Keep public APIs small and explicit.
- Prefer simple value types and clear ownership.
- Do not add external dependencies directly to random source files; wire them through CMake and document the dependency.
- Reflection metadata should become the single source for serialization, editor inspectors, schemas, docs, and Lua stubs as those systems are implemented.
- Keep 2D and 3D component concepts consistent where practical, for example transform parenting, collider metadata, and Lua-facing naming.
- Renderer debug visuals should match runtime behavior. If collision uses a sphere, draw a sphere debug collider; if it uses a box, draw a box.
- When changing movement or collision, add focused regression coverage in `tests/` rather than relying only on manual example play.

## Lua Rules

- Lua gameplay scripts should use explicit lifecycle functions: `on_create`, `on_start`, `on_update`, `on_fixed_update`, and `on_destroy`.
- Do not expose raw C++ internals to Lua.
- Add LuaLS annotations or generated stubs when adding public Lua APIs.
- Keep Lua APIs high-level and stable. Prefer `Entity`, `Transform2D`, `Transform3D`, `Network`, and `NetworkSession` style services over exposing raw component storage.
- Remember `Input.axis(negative, positive)` returns positive minus negative.
- For network/session Lua payloads, preserve array-like tables when encoding/decoding so vector/color data round-trips as numeric indexes.

## Transform And Physics Rules

- `Transform2D.parent` and `Transform3D.parent` are stable IDs, not object pointers.
- Parent transforms should be resolved through runtime helper functions before rendering, camera positioning, or editor display.
- Scripted 3D movement through `Transform3D.set_position` and `Transform3D.add_position` should respect dynamic rigidbody colliders against static colliders.
- Collider component shape matters. Do not approximate sphere gameplay/debug behavior with box logic unless the component is explicitly a box.

## Adding Components Later

- Define the C++ component type.
- Add reflection metadata.
- Add serialization fields.
- Add validation rules.
- Add editor inspector metadata.
- Add Lua bindings only when gameplay needs them.
- Add scene round-trip and validation tests.

## Adding Editor Panels Later

- The editor panel should use runtime/editor services, not private duplicated state.
- Any scene mutation should map to a command-like operation.
- Validation errors must come from the same diagnostics path as the CLI.
