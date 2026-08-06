# Capability Manifest and Reference Gates

Phase 0 makes engine progress testable in three ways:

1. `demi capabilities export` inspects the installed Lua VM and component
   descriptors and writes a deterministic machine-readable manifest.
2. `demi capabilities check` compares that live manifest with the checked
   public API baseline. Removals, type changes, newly required fields, and
   replication-contract changes fail. Additions are reported for review.
3. `demi capabilities verify-gates` validates every reference project and
   rejects project source that refers to engine-private, build, generated-only,
   or editor-only state.

## Commands

```sh
./build/linux-debug/demi capabilities export
./build/linux-debug/demi capabilities export \
  --output build/linux-debug/generated/capabilities.json
./build/linux-debug/demi capabilities check
./build/linux-debug/demi capabilities check --format json
./build/linux-debug/demi capabilities verify-gates
```

The checked inputs are:

- `capabilities/public_api.baseline.json`
- `capabilities/reference_gates.json`

## Changing a public API

Run the compatibility check first. A breaking diagnostic should normally be
fixed by restoring compatibility. If the break is intentional and versioned,
review the generated manifest and replace the baseline in the same change.
Compatible additions also produce informational diagnostics until the
baseline is reviewed.

The Lua portion is sourced from functions actually installed into a fresh Lua
runtime. The component portion is sourced from `componentDescriptors()`.
`demi-lua-stub-contract-tests` independently requires the installed API and
`scripts/stubs/demi.lua` to agree.

## Adding a reference game

Keep the complete gate under `examples/`, add its project to
`reference_gates.json`, and register its named validation, runtime, and
packaging tests with CTest. List known missing reusable engine behavior in the
top-level `gaps` array and reference those IDs from the gate. Do not hide the
gap in an example-only workaround.

The gate verifier scans authored `.json` and `.lua` files. References to
`src/demi/`, `build/`, `generated/`, `.codex/`, `editor://`, or
`generated://` fail the gate. Normal project-local source and validated
`asset://`, `prefab://`, `scene://`, and `script://` references remain valid.
