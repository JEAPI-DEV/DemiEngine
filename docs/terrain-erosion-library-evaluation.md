# Terrain erosion library evaluation

Reviewed 2026-10-13. This is a source-level milestone-3 evaluation. No upstream
library was installed, linked or benchmarked; no comparative speed or visual
quality result is claimed. Existing native erosion predates this review.

## Recommendation

Keep the native stage as the current shared implementation. Evaluate HighMap in
an isolated offline comparison before adopting another solver. Integration is
not justified by its feature list alone: licensing, cancellable execution and
portable repeatability remain unresolved. This review closes the initial
library survey, not the plan's representative-comparison acceptance gate.

## Upstream candidates

HighMap was reviewed at commit
[`4fb63ae6ccd793b6e1e3bfa86a2c1a124c7a43dd`](https://github.com/ottolink-dev/HighMap/commit/4fb63ae6ccd793b6e1e3bfa86a2c1a124c7a43dd),
dated 2026-09-21. That is evidence of recent development, not a support guarantee.
Its [erosion API](https://github.com/ottolink-dev/HighMap/blob/4fb63ae6ccd793b6e1e3bfa86a2c1a124c7a43dd/HighMap/include/highmap/erosion.hpp)
provides hydraulic, thermal and combined models, erosion/deposition outputs,
masking and bedrock inputs. These make it a useful algorithm-comparison target.

Its license is currently ambiguous: the
[root LICENSE](https://github.com/ottolink-dev/HighMap/blob/4fb63ae6ccd793b6e1e3bfa86a2c1a124c7a43dd/LICENSE)
is LGPL-2.1, while the
[README](https://github.com/ottolink-dev/HighMap/blob/4fb63ae6ccd793b6e1e3bfa86a2c1a124c7a43dd/README.md)
says GPL-3.0 and the reviewed erosion source headers say GNU GPL. Resolve the
intended license and audit the
[third-party notices](https://github.com/ottolink-dev/HighMap/blob/4fb63ae6ccd793b6e1e3bfa86a2c1a124c7a43dd/THIRD_PARTY_LICENSES.md)
before copying code or shipping a linked dependency. This is an integration
blocker, not a conclusion that every upstream file has one particular license.

The [root CMake](https://github.com/ottolink-dev/HighMap/blob/4fb63ae6ccd793b6e1e3bfa86a2c1a124c7a43dd/CMakeLists.txt)
unconditionally requires GSL, Assimp, GLM, OpenMP, OpenCV and OpenCL, despite an
OpenCL option. Release defaults include fast-math. Its
[library target](https://github.com/ottolink-dev/HighMap/blob/4fb63ae6ccd793b6e1e3bfa86a2c1a124c7a43dd/HighMap/CMakeLists.txt)
builds all source files into a static library and exposes numerous transitive
dependencies. Using one solver is not currently a small dependency addition.
Linux feasibility does not qualify Android or bgfx/Vulkan portability.

The reviewed
[particle implementation](https://github.com/ottolink-dev/HighMap/blob/4fb63ae6ccd793b6e1e3bfa86a2c1a124c7a43dd/HighMap/src/erosion/hydraulic_particle.cpp)
runs OpenCL passes, mutates its array and returns erosion/deposition maps. It
accepts a seed, but that alone does not establish identical outputs across
OpenCL devices, thread scheduling or compiler math modes. The reviewed APIs
have no stop-token or progress callback; bounding one call per pass would still
need measurement of latency and proof that slicing preserves algorithm state.

[soillib](https://github.com/erosiv/soillib) is another maintained geomorphology
candidate, reviewed at commit `9b9e0871a54b0a106ebdf9c1f4d210c7290ec0d4`
(2026-07-08). Its upstream README describes C++23/CUDA, Python bindings,
LGPL-3.0 licensing and Windows/Linux testing. CUDA, a newer language baseline
and its support libraries make it unsuitable as DemiEngine's shared Linux and
Android erosion dependency today. It remains a possible offline research tool;
its performance and reproducibility were not qualified here.

## Native implementation and current evidence

[TerrainErosion.cpp](../src/demi/runtime/terrain/TerrainErosion.cpp) already
implements seeded droplets that erode, carry and deposit sediment along a
supplied drainage tree, followed by six buffered thermal passes. It returns a
new sample set, preserves the source and publishes signed surface change as
sediment. Stop-token checks occur during copies, droplet batches, thermal loops
and final publication. It adds no external dependency or graphics requirement.

This is a bounded terrain-processing approximation, not a full fluid solver.
Drainage is fixed during erosion. Independent droplet random streams do not
make the mutated heightfield independent of droplet execution order; the current
serial ordering is part of repeatability. Cross-platform bitwise equality is
not proven. The follow-up corrects thermal distances to use both grid axes and Euclidean
diagonals. Standard droplet population still grows with iteration count inside an iteration loop, making cost roughly
quadratic in that setting before its cap.

[terrain_erosion_tests.cpp](../tests/terrain_erosion_tests.cpp) covers small
analytic fields: repeatability, seed changes, immutable inputs, scouring and
deposition, thermal isolation, Preview work reduction, signed balance and grid
mismatch rejection. Release assertions are enabled. Follow-up work added
anisotropic conservation/stability and cancellation cases, plus original-grid
500-by-500 Preview/Final runs. See
[generation quality qualification](terrain-generation-quality-qualification.md).
These do not establish cross-platform determinism or a measured upstream solver
comparison. Remaining droplet loads are deposited at termination; small balance
residuals arise from floating-point updates, not deliberate load deletion.

## Required comparison probe

Use the same immutable ridge/valley input for native hydraulic-only, thermal-only
and combined runs, plus one pinned HighMap model after licensing is resolved.
Include flat, spike and anisotropic analytic fields. Record physical cell sizes,
solver settings, seed, source revision, build flags and device. Compare signed
cut/deposit totals, finite heights, slope distributions, repeated output hashes,
cancellation latency, peak memory and generation time. Retain before/after wide
and close images with identical camera and lighting. Do not compare iteration
counts as if different algorithms perform equivalent work. Preserve the authored
heightfield and publish replacements only after successful uncancelled completion.
