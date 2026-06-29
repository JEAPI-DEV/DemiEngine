# File Formats

The scaffold currently uses JSON because it is easy to validate, parse, diff, and generate. YAML or TOML can be added later where they provide a clear benefit.

## Project Files

Project files use `*.project.json` and declare the project name, scenes, and metadata.

## Scene Files

Scene files use `*.scene.json` and declare stable scene IDs and entity lists.

## Save Files

Save files use `*.save.json` and declare save slot metadata plus game-specific state.

## Asset Manifests

Asset manifests use `*.asset.json` and map stable `asset://` ids to source files. The first supported runtime texture source is text PPM (`P3`) for `Texture2D` assets.

## Versioning

Every format includes `format_version`. Migrations should be explicit and test-covered.
