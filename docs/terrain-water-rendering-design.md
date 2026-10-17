# Terrain water rendering architecture

Status: implemented on 2026-10-17; see [qualification](terrain-milestone5-qualification.md).
The prerequisite audit below records the original constraints and sequence.
The implementation uses an independent single-sample R32F depth prepass,
normalized camera-forward eye depth, and an explicit target transition before
copying resolved opaque colour. The main scene retains its authored MSAA.
Camera ranges reserve twelve views; water, presentation and HUD passes have
distinct slots. Queries and optical camera sampling share prepared water data.
Milestone 5 currently publishes actual clipped river/lake/ocean meshes. Native
water queries retain their own prepared coverage and do not depend on rendering.

## Starting renderer constraints

`TerrainWaterMesh` expands prepared water triangles into a transparent mesh,
with depth-derived vertex colours, roughness and local XZ UVs. The ordinary
mesh pass lights and blends that surface. It does not perform refraction,
reflection, animated waves or underwater camera effects.

The renderer exposes render-target colour and depth handles, but the bgfx
adapter creates MSAA depth with `BGFX_TEXTURE_RT_WRITE_ONLY`. A depth handle
alone therefore does not establish that a shader can sample it. Rendering
directly to the backbuffer also provides no sampleable scene-colour snapshot.
Sampling the attachment currently being written would create feedback.

The eight camera view slots are already allocated to shadows, scene and
presentation/overlays. A water pass cannot safely use an unexplained next view
ID. Camera pass allocation and capacity validation must change together.

## Implemented sequence

1. Add a camera-owned opaque-scene capture boundary in the shared renderer.
   Separate opaque and transparent submission. Retain resolved opaque colour
   and a deliberately sampleable depth representation before water draws.
   Preserve authored MSAA; do not silently switch the scene to single-sample
   rendering. Validate depth resolve or an explicit linear-depth path against
   backend capabilities, and report unsupported configurations clearly.
2. Give camera passes named, non-overlapping slots. Capture lifetime, resize,
   target recreation and failure cleanup belong beside existing camera target
   ownership. Both editor render targets and standalone cameras use this path.
3. Publish native water metadata alongside the existing clipped mesh: stable
   body identity, appearance and the prepared data needed for surface shading.
   Terrain generation/publication remains independent of bgfx. A renderer-owned
   water surface renderer consumes these resources; gameplay continues querying
   prepared water without a render-resource dependency.
4. Use opaque scene depth and the same camera projection to compute the optical
   path through water. Apply absorption and depth-clamped refracted sampling.
   Decode existing display-encoded scene colour before optical calculations.
   Add Fresnel environment reflection from the actual environment resource;
   label it accurately as environment reflection, not reflected scene geometry.
5. Add animated normals and shoreline/flow foam using body data. Normal-only
   animation does not move gameplay surfaces. Geometric waves require an
   explicit shared time/displacement convention before floating probes can
   agree with visible water. Underwater effects and opt-in buoyancy/swimming
   follow the same query contract.

## Boundaries and qualification

An opaque snapshot cannot refract transparent objects drawn later. That initial
limitation must be documented. Environment reflection cannot show a nearby
building unless another reflection technique supplies it. Planar reflections
alone also do not cover sloping rivers.

Transparent mesh sorting currently uses transform origins; water children share
the terrain owner's origin. River/lake joins and overlapping surfaces need
explicit coverage and ordering tests rather than reliance on equal sort keys.

Tests must cover camera resize/recreation, separate cameras, source/cooked
parity, transformed water, above/below-water views, shallow intersections and
MSAA. Measure opaque capture, water and presentation separately with the retained
performance diagnostics. Keep query results invariant when water is culled or
render detail changes. Full fluid simulation is outside this milestone.
