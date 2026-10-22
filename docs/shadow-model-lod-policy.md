# Sampling-aware static shadow model LOD

Implementation and qualification record, 2026-10-22. Focused regressions and a
native Linux Release trace exercise this policy. It makes no pixel-equivalence
claim; visual acceptance of LOD silhouettes and transitions remains open.

Directional shadow cascades already carry a stabilized orthographic diameter
and per-tile resolution. Their world-space texel footprint is
`2 * cascade.radius / shadowResolution`. For a perspective main camera, the
renderer converts this footprint into an equivalent main-camera distance:

```
focalPixels = effectiveMainPixelHeight / (2 * tan(verticalFov / 2))
shadowDistanceFloor = worldUnitsPerShadowTexel * focalPixels
lodDistance = max(actualCameraDistance, shadowDistanceFloor)
```

The effective main height includes render scale or the actual authored
RenderTarget height. The shadow atlas's combined dimensions do not replace its
individual cascade resolution. The distance floor applies only to selecting
existing static model LODs. Main-view geometry and authored distance culling
continue using the actual camera distance. Moving the LOD camera position to
approximate this policy would incorrectly change culling and is not permitted.

Orthographic main cameras have a constant world-space pixel footprint and no
perspective equivalent distance; they retain the previous authored-distance
policy. Animated models, dentable meshes, active dent data and surface relief
also retain their previous policy. A per-entity animated geometry cache must not
be switched between independent cascade models without its own lifetime work.
Static foliage wind remains an appearance effect of the authored LODs.

The renderer's shared model-selection helper supplies both imported material
section lookup and mesh submission. Their selected model and index ranges must
agree. Existing bounds use the union of authored static LOD bounds; this policy
does not narrow visibility bounds, alter terrain LOD, remove casters by a new
distance rule, or change gameplay collision. It needs no new GPU feature or
public authoring field.

This is a sampling-aware heuristic, **not an error bound**. Existing LOD assets
have authored distance thresholds, not measured geometric or alpha-coverage
error. A truly bounded policy would require per-LOD deviation information,
conservative scale handling and separate foliage coverage qualification.
Reduced-model silhouettes, alpha cutoff, shader wind and cascade overlap can
still differ; visual qualification must inspect them rather than infer that all
changes are below one shadow texel.

`Renderer3D.shadow.<cascade>.world_units_per_texel` and
`model_lod_distance_floor` expose the derived scale. The same prefix reports
`lod_medium`, `lod_low`, `triangles` and `batches` for comparisons. Main counts
remain separate. Record resolution, render scale, camera path, shadow quality,
source revision and build configuration alongside every performance result.

Focused tests cover the sampling derivation and scaling, actual-distance cull
independence, main-view/static/animated/deformation exclusions, conservative
cascade progression and imported per-LOD section ranges. The final Silvermere
trace retains thousands of non-grass casters while reducing shadow work; see
[showcase qualification](silvermere-showcase.md). Its gains combine this heuristic,
receiver-hull culling and the requested grass no-cast policy and cannot be
attributed to this heuristic alone. Native visual acceptance still requires fixed-camera and moving-camera/light comparisons of close cutout
foliage, distant casters and cascade overlap, plus repeatable CPU/GPU traces.
