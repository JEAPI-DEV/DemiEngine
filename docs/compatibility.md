# Compatibility Policy

DemiEngine is in alpha/beta development. There has not been a production release,
and current development builds do not promise backward compatibility.

This policy covers public Lua APIs, package interfaces, and project, scene,
prefab, HUD, asset and save formats.

## During alpha/beta

Breaking changes are allowed when they improve the engine's design or usability.
APIs, packages and fields may be renamed, replaced or removed without a
deprecation window. Old destruction checkpoints and other development saves
may require a reset.

- Maintain one current way to use a feature. Do not add aliases, duplicate
  loaders or compatibility branches solely to preserve development-era behavior.
- Migrate the repository's examples, templates and package sources with the
  implementation. Update schemas, Lua stubs, tests and documentation in the
  same change.
- Explain breaking changes and any required rebuild, reinstall or save reset.
  A migration tool is optional when it saves useful work; it is not a release
  requirement during this phase.
- Do not silently delete saves or rewrite unrelated user data. Permission to
  break compatibility is not permission to discard data.

A version number on a development package does not make the engine stable.
Package locks, content hashes and validation remain necessary for reproducible
installs; they must not be bypassed to accommodate an API change.

## Format versions and errors

Durable documents still require `format_version`. Readers must reject
unsupported versions and invalid data with clear diagnostics rather than guess
or silently drop fields. Advance a format version when necessary to distinguish
incompatible meanings of otherwise valid data.

Unknown engine components remain errors unless a registered extension owns them.
Gameplay data belongs in declared serializable data, script properties or saves.
Removing compatibility guarantees does not relax validation or data integrity.

## First production release

The first explicitly designated production release will establish the supported
compatibility baseline. Before that release, define the guarantees for patches,
the deprecation process and the migration policy for supported versions.

Those guarantees will apply from that baseline onward. They will not require
supporting every alpha/beta format or API retrospectively.
