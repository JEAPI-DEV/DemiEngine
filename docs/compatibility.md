# Compatibility Policy

This policy applies to project, scene, prefab, HUD, asset, and save formats and
to public Lua APIs.

## Versioned Data

- Every durable document includes an integer `format_version`.
- Version 1 is the current project, scene, HUD, asset, and save baseline unless
  a schema states otherwise.
- A reader must reject unsupported future versions with a structured
  diagnostic; it must not guess.
- A format change that alters meaning, removes a field, or changes defaults
  requires a deterministic migration and round-trip tests.
- Migrations operate on authored data explicitly. The editor may invoke them,
  but must not silently keep an editor-only upgraded representation.

## Deprecation Window

- Public formats and Lua APIs are deprecated for at least one minor release
  before removal when a compatibility adapter is practical.
- Deprecations must identify the replacement and intended removal version in
  release notes and diagnostics.
- Security flaws or data-corruption bugs may require immediate removal; that
  exception must include a migration or recovery note.

## Compatibility Guarantees

- Patch releases preserve valid authored version-1 documents and public Lua
  API behavior except for bug fixes.
- Minor releases may add optional fields, components, and APIs with stable
  defaults.
- Major releases may remove deprecated behavior and advance format versions,
  with checked-in migrations for supported source versions.
- Unknown engine components are errors unless an explicitly registered
  extension owns them. Gameplay data must live in declared serializable data,
  `LuaScript.properties`, or saves; it must not disappear during loading.

## Required Change Checklist

Any public format/API change updates its schema, documentation, migration or
deprecation notice, Lua stubs when applicable, and focused regression tests.
