# Shared glTF import qualification

2026-10-21. The existing cgltf dependency now owns glTF/GLB parsing, buffer and
accessor decoding, and node transforms for static geometry, skinned models and
metadata-based collider bounds. No second library or alternate parser was added.

`GltfDocument.h/.cpp` is a private ownership adapter: one prefixed cgltf
implementation, unique_ptr lifetime, metadata parsing/validation, explicit buffer
loading/validation and attribute lookup. Prefixes remain necessary because bgfx
also embeds cgltf. Upstream types stay inside asset implementation files.

Removed mechanisms include the handwritten static accessor/buffer decoder and
two sets of JSON TRS/matrix traversal. Including the new adapter, the asset
consolidation has a textual net reduction of 247 production C++ lines: 504
removed from existing implementations, 133 added there, and 124 in the new
adapter. This includes moving symbol-prefix and lifetime code; it is not a
claim that 247 lines of algorithms disappeared or that the entire engine shrank. Tests and
qualification documentation are additional evidence, not counted as production
code reduction. The exact total can change with final formatting.

The retained code owns engine behavior, not duplicate parsing:

- Static geometry selects the default scene, or the first scene when omitted.
  It extracts triangle primitives, indexed/nonindexed positions and optional UVs.
- Skinned model import retains all mesh-owning nodes, skin/joint/weight data,
  inverse bind matrices, clips, images and bind-pose evaluation.
- Metadata bounds include POSITION accessors for all primitive modes. The
  zero-detail glTF collider path uses accessor min/max and eight transformed
  corners, preserving import-axis/unit conversion and minimum-size policy.
- Metadata parsing does not load external buffers. Collider generation still
  hashes declared source dependencies: a missing buffer can therefore fail hash
  verification even when its metadata is sufficient to calculate bounds.
- Positive-detail glTF generation still validates actual triangle geometry.
  Collider policy, deterministic output and transactional writes remain owned
  by Demi rather than cgltf.

Malformed references now fail through the shared parser. Some old distinct
scene/node/mesh-reference diagnostic codes consolidate into
COLLIDER_GLTF_STRUCTURE_INVALID (with source path); invalid JSON retains its
parse diagnostic family. No second JSON parser was kept merely to reproduce
old malformed-reference distinctions. Missing scenes or accessor bounds still
receive specific diagnostics when the parsed structures are available.

Focused regressions cover default-scene versus all-node behavior; nested,
mirrored quaternion/matrix transforms; import scaling/axis conversion;
interleaved accessor offsets; 8/16/32-bit indices; nonindexed geometry; UV
presence/defaults; embedded GLB; out-of-range indices; missing/truncated buffers;
and metadata-only collider generation with truncated payloads.

An unrelated existing GPU skinning test exposed stale counters after the last
animated entity disappeared. The old conditional skipped the lifecycle method
that reset counters. Empty animation membership now executes its empty lifecycle
without scanning static objects or allocating an animation batch; retention
cleanup continues through the existing owner. The regression asserts zero CPU,
GPU, batch and deferred counts as well as no palette/vertex work.

## Results

Release static/skinned import, GPU skinning, lightweight 3D workflow and renderer
suites pass. The final Amber Pass native E2E also passes, exercising the imported
horse and environment through staged loading and live animation. Example
validation remains clean. The retained import-policy differences are covered by
fixtures rather than replaced with one supposedly equivalent skinned path.

The catch-all asset resource loader and broader JSON Schema replacement remain
separate audit items; they were not silently changed as part of import cleanup.
