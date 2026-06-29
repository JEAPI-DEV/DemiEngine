# Agent Rules For DemiEngine

This repository is designed for AI-assisted development. Keep source data deterministic, validate changes through the CLI, and avoid hidden editor-only state.

## Build And Test

- Configure: `cmake --preset linux-debug`
- Build: `cmake --build --preset linux-debug`
- Test: `ctest --preset linux-debug`
- CLI help: `./build/linux-debug/demi --help`
- Validate examples: `./build/linux-debug/demi validate examples/minimal_2d/demi.project.json`

## Source Vs Generated Files

- Source files are edited by humans and agents.
- Generated files belong in `build/`, `generated/`, or `examples/**/generated/`.
- Do not hand-edit generated files.
- Do not commit build outputs.

## Data Format Rules

- Every project, scene, save, and future asset manifest must include `format_version`.
- Asset manifests use `*.asset.json` and stable `asset://` ids.
- Prefer stable IDs over positional references.
- Prefer URI-style references such as `scene://main`, `asset://textures/unit.png`, and `script://units/archer.lua`.
- Keep formatting stable so diffs are small and reviewable.
- Run `demi validate` after editing example project data.

## C++ Rules

- Use C++20.
- Keep public APIs small and explicit.
- Prefer simple value types and clear ownership.
- Do not add external dependencies directly to random source files; wire them through CMake and document the dependency.
- Reflection metadata should become the single source for serialization, editor inspectors, schemas, docs, and Lua stubs as those systems are implemented.

## Lua Rules

- Lua gameplay scripts should use explicit lifecycle functions: `on_create`, `on_start`, `on_update`, `on_fixed_update`, and `on_destroy`.
- Do not expose raw C++ internals to Lua.
- Add LuaLS annotations or generated stubs when adding public Lua APIs.

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
