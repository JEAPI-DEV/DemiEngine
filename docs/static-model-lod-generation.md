# Static model LOD generation

The shared asset pipeline generates ordinary Model3D variants from project-owned
glTF/GLB models. The editor action and `demi asset lod` call the same generator;
the renderer continues to use authored `medium_lod_model`, `low_lod_model` and
distance fields. Generated assets do not add a runtime LOD format or a second
model importer.

## Geometry and appearance

The pinned bgfx bundle supplies meshoptimizer 0.25. The asset subsystem links only
its simplifier and allocator. Every primitive/material is simplified independently
with attribute error for UV coordinates, normals and vertex colors. Open borders
are locked. Error constraints and topology can prevent the requested ratio;
results report actual input/output triangles and achieved error instead of
forcing geometry loss. Retained vertex attributes are copied without modifying
UVs, normals, colors or tangents, and streams are compacted to referenced vertices.

The generated self-contained GLB retains the source node hierarchy/transforms,
material assignments, images and independent U/V sampler settings. Original
source bytes stay unchanged. Original buffer payloads are retained before compact
replacement streams are appended, so this first version prioritizes rendering
and vertex reductions over minimum disk file size. Installed package manifests
are immutable; import a project-owned copy before adding a generation recipe.
Skins, animation, morph targets, unsupported compression and non-triangle
primitives produce explicit diagnostics instead of silently flattening behavior.

## Ownership, publication and regeneration

Variants use `<source asset ID>/lod/medium` and `/lod/low`. Their GLBs/manifests live
under `assets/generated/model_lods/<source-id hash>/`. An ownership record prevents
replacement of unrelated files or conflicting stable IDs. Generated payloads are
software outputs and are not checked into source control.

The original model manifest retains `settings.model_lods` with `format_version`,
`medium_ratio`, `low_ratio` and `max_error`. Ordinary source reimport regenerates
both variants using this recipe. Generated manifests copy the original import
profile and record their own provenance. The recipe contains no variant asset
references, preventing a generated-to-original-to-generated dependency cycle.
Build/cook preparation runs before validation and regenerates missing or stale
variants from this recipe, so a fresh checkout does not require committed output
files. Provenance and actual input payloads determine whether variants are current;
unchanged outputs and manifests keep their timestamps, including an explicit
repeat generation. Validation stays read-only and reports
`MODEL_LOD_GENERATION_REQUIRED` with a reimport/build/cook suggestion for declared
variants that have not been generated. Ordinary cook preserves the manifest recipe
and generated asset identities.

Preparation happens in an owned project-local workspace. Output directories,
manifest and legacy generated mirror are published as one rollback group only
after inputs are rechecked. Source manifest edits use the shared authored JSON
patcher, preserving unrelated keys, formatting and compact values. Publication rejects incompatible destination types,
unsafe symlinks, overlapping paths and unowned output files. An ordinary failure
restores prior artifacts; a recovery failure retains backups and reports their
location. These are process-level filesystem transactions, not crash-durable
multi-file commits.

Applying generated references in the editor is a separate validated document
command with Undo/Redo. It requires a selected matching model and preserves all
other fields, including existing LOD distance thresholds. A generation success is
not lost if no matching scene selection is available.

## Qualification

Focused targets are `demi-model-lod-generation-tests`,
`demi-editor-workspace-tests`, `demi-gltf-materials-tests`, and the CLI/editor
builds. The generation fixture exercises actual geometry reduction, compact
streams, material/sampler/image preservation, deterministic output hashes,
recipe reimport, registry validation, fresh-checkout cooking, input freshness,
unchanged timestamps and unchanged prior outputs on
unsupported inputs or unsafe destinations.

The Linux Release combined build completed for the CLI/editor and focused test
targets. Eight focused suites passed in 4.84 seconds: model LOD generation,
editor workspace, asset pipeline, imported glTF materials, editor diagnostics,
editor Play session, runtime profiler and frame timing. The follow-up Linux Release nine-suite gate also passed with source-manifest
formatting preservation and its regression. This record does not claim native
LOD visual or physical Android qualification.
