# Terrain erosion library evaluation

Updated 2026-10-14. The initial HighMap/soillib source survey was followed by
an isolated libtcod comparison on identical native float32 inputs. No upstream
solver was integrated into DemiEngine. Numerical and timing evidence follows;
no comparative visual-quality result is claimed.

## Recommendation

Keep the native stage for now because it shares the declared drainage,
thermal transport, progress and cancellation contracts. This is **not** a
performance recommendation: libtcod's narrower hydraulic model took substantially
less solver time on the representative field. Its permissive license and
repeatable batched execution justify keeping it as a concrete alternative.
Replacing the native solver still requires visual acceptance, cancellation
latency and cross-platform qualification, including Android. HighMap remains a
broader historical candidate with the integration blockers recorded below.

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

[TerrainErosion.cpp](../src/demi/runtime/terrain/TerrainErosion.cpp), generation 9,
implements seeded droplets following currently descending terrain mesh edges.
Prepared drainage guides ties and flat choices without forcing an uphill step.
Open-edge sediment exits the field; talus-limited settling and one-ring spreading
leave excess unfinished transport explicitly suspended. Six buffered thermal
passes follow in Final when thermal strength is positive. A contiguous work buffer is returned as a new sample
set, preserving the source. Signed surface change and a separate gross hydraulic
ledger are published only on success. Stop-token checks occur during copies,
droplet batches, thermal loops and publication. No external dependency or
graphics requirement is added.

This is a bounded terrain-processing approximation, not a full fluid solver.
The drainage hint is fixed, but descending choices read the current surface.
Independent droplet random streams do not
make the mutated heightfield independent of droplet execution order; the current
serial ordering is part of repeatability. Cross-platform bitwise equality is
not proven. The follow-up corrects thermal distances to use both grid axes and
Euclidean diagonals. Standard droplet population still grows with iteration
count inside an iteration loop, making cost roughly quadratic in that setting
before its cap.

[terrain_erosion_tests.cpp](../tests/terrain_erosion_tests.cpp) covers small
analytic fields: repeatability, seed changes, immutable inputs, scouring and
deposition, thermal isolation, Preview work reduction, signed balance and grid
mismatch rejection. Release assertions are enabled. Follow-up work added
anisotropic conservation/stability and cancellation cases, plus original-grid
500-by-500 Preview/Final runs. See
[generation quality qualification](terrain-generation-quality-qualification.md).
These tests do not establish cross-platform determinism. The separate measured
upstream comparisons are below. Generation 8 deposited remaining loads at
termination; generation 9 records export and unfinished suspension instead of
forcing all sediment back onto the surface.

## Historical generation-8 libtcod comparison

The coordinator installed `tcod==21.2.1` only in
`/tmp/demi-erosion-comparison-venv`. The
[release source](https://github.com/libtcod/python-tcod/tree/21.2.1) pins libtcod
submodule
[`27c2dbc9d97bacb18b9fd43a5c7f070dc34339ed`](https://github.com/libtcod/libtcod/tree/27c2dbc9d97bacb18b9fd43a5c7f070dc34339ed),
not the newer `main` snapshot examined during the source assessment. The C library's
[BSD-3-Clause license](https://github.com/libtcod/libtcod/blob/27c2dbc9d97bacb18b9fd43a5c7f070dc34339ed/LICENSE.txt)
is materially simpler to accommodate than HighMap's unresolved notices. The
comparison introduced no repository or system dependency.

Inputs and native outputs were exported by
[benchmark_terrain_generation.cpp](../scripts/benchmark_terrain_generation.cpp);
[compare_terrain_erosion.py](../scripts/compare_terrain_erosion.py) verified their
FNV digests before running libtcod. Artifacts for this run are
`/tmp/demi-erosion-comparison/export.json`, `report.json` and adjacent little-endian
float32 files; these temporary paths are not durable qualification storage.
The recorded source revision is `26138396-plus-river-work`, generator version 8,
build label `linux-release`, optimized GCC 16.2.1 probe, Linux x86_64/glibc 2.44,
Python 3.14.7 and NumPy 2.5.4. This records a working-tree snapshot rather than
claiming a clean commit identity; canonical recipe and height digests are retained.

Four inputs were exercised: 16-by-16-cell flat and spike/pit fields at one-unit
spacing, a 64-by-16-unit anisotropic spike field at 4-by-1-unit spacing, and
the rocky valley's `valley_blend` output before authored erosion and water.
The representative input has 501-by-501 samples across 2,000-by-2,000 units,
four-unit spacing, seed 1704 and digest `fnv1a64:70e1cd8fee34b31d`.
Palette dependencies were explicitly excluded; heights were not normalized.

Representative solver results from two uninterrupted runs:

| Solver mode | Milliseconds | Mean absolute height change | Net sample-height change | Physical slope p95 |
| --- | ---: | ---: | ---: | ---: |
| Native hydraulic, 16 iterations | 405.28–415.13 | 5.5661 | −0.01761 | 3.2405 |
| Native thermal only, six passes | 81.58–82.20 | 0.8128 | +0.00199 | 2.3170 |
| Native combined | 508.40–510.92 | 5.5506 | −0.01908 | 2.4631 |
| libtcod rain erosion | 109.12–110.69 | 4.8374 | +0.00570 | 3.3283 |

Native hydraulic and libtcod both ran 250,992 droplets. Matching that count
does not match work or behavior. Native droplets traverse fixed, prepared
drainage on the terrain's six triangle edges, with capacity, gravity,
evaporation and a 96-step limit. The pinned
[libtcod implementation](https://github.com/libtcod/libtcod/blob/27c2dbc9d97bacb18b9fd43a5c7f070dc34339ed/src/libtcod/heightmap_c.c)
chooses the largest current height drop among eight neighbors until a sink,
without those state variables or a step bound. Its coefficient pair was
0.05/0.05, using an explicitly seeded Mersenne Twister. That pair balances its
cut and deposit rule; it is not equivalent to native erosion settings.
libtcod supplies no thermal counterpart in this comparison.

The native hydraulic timer includes its initial output copy and erosion work;
libtcod's timer excludes the caller's input copy and RNG setup. Both exclude
file I/O, digest checks and metric computation. Native drainage was separately
58.43 ms; baseline generation was 123.07 ms. Thus even the solver timings have
different boundaries, and they are generation timings, not frame-time results.
The wheel's compiler flags were not recorded; two runs are a small sample,
not a statistical performance qualification.

All outputs were finite. Native repeat digests matched on this binary and
machine. libtcod repeats and 64-drop batches with the same mutable field and RNG
were byte-identical to its uninterrupted call on all four inputs. The representative
batched call took 127.45 ms including Python/C call overhead. This supports
checking cancellation **between** batches, but no cancellation deadline was
measured and an individual call cannot be interrupted through this API.
It does not establish cross-platform bitwise determinism.

Transport metrics are sums of sample-height differences, not physical mass.
Slope metrics divide finite differences by the actual X/Z spacing; libtcod's
erosion function itself has no physical-spacing argument. The representative
outputs differed by 5.3035 units on average and 232.04 units at maximum.
Maximum sampled slopes were 74.66 for native hydraulic, 54.14 combined and
26.70 for libtcod; these large outliers warrant visual review, not a quality
ranking. The flat field stayed unchanged for every mode. Python process
high-water RSS was 67,976 KiB, including imports and all cases; this is not
solver-only memory and no matched native memory comparison was captured.

Subsequent native close-up inspection identified terminal-deposition piles of
about +272 units at a border and +252 units in the interior before thermal
relaxation. This generation-8 result is retained as failure evidence, not a
qualified erosion appearance. The generation-9 change below addresses that
settling behavior; the old numbers must not be presented as current results.

## Generation-9 measured follow-up

The coordinator reran the same tools with generator version 9 and source label
`26138396-plus-generation9-work`, using the same Release probe configuration,
machine, Python/NumPy versions and pinned libtcod wheel described above.
Artifacts are `/tmp/demi-erosion-comparison-v9/export.json`, `report.json` and
their float32 sidecars. These paths are temporary. All four baseline digests,
including representative `fnv1a64:70e1cd8fee34b31d`, match generation 8.
The representative field and 250,992 hydraulic droplets are unchanged; routing,
settling, accounting and the work buffer are changed. This is **not** a pure
optimization comparison of an unchanged hydraulic algorithm.

| Solver mode | Milliseconds, two runs | Mean absolute height change | Net sample-height change | Physical slope maximum |
| --- | ---: | ---: | ---: | ---: |
| Native hydraulic | 211.66–218.70 | 4.4902 | −465,977.6112 | 53.4088 |
| Native thermal only | 47.73–47.83 | 0.8128 | +0.0020 | 4.1752 |
| Native combined | 261.43–273.74 | 4.6013 | −465,977.6054 | 6.1756 |
| libtcod rain erosion | 108.78–109.39 | 4.8374 | +0.0057 | 26.7020 |

Native baseline generation was 125.87 ms and drainage 56.09 ms, outside the
solver ranges. Copy/timing boundaries, unrecorded wheel compiler flags and the
small sample size retain the earlier limitations. libtcod still supplies no
thermal stage. Current native hydraulic uses six dynamic mesh-neighbor choices
with a drainage hint; libtcod uses eight current steepest-drop choices. Matching
seeds and drop totals does not match paths, transport models or completed work.

The hydraulic-only and combined runs have the same **gross** hydraulic ledger:

| Outcome | Discrete sample-height units |
| --- | ---: |
| Eroded | 900,473.1707 |
| Deposited | 434,495.5595 |
| Exported through open edges | 38,755.6522 |
| Suspended unfinished transport | 427,221.9590 |

Gross deposition includes material subsequently eroded again, so it differs
from the final surface's positive height differences. Suspended transport is
about 47.4% of gross erosion: a substantial unfinished budget, not a retained
fluid/sediment field or a promise that the next run will settle it. The surface
is therefore not a closed-mass result. Hydraulic ledger residual is zero;
surface net change plus exported and suspended totals is zero for hydraulic
and +0.00574 for combined, whose surface also includes thermal rounding.
Thermal-only net change is +0.00199. These are numerical accounting checks in
sample-height units, not measured physical sediment mass.

All outputs remain finite. Native repeat height digests and hydraulic ledgers
match, and libtcod repeat/batch outputs remain byte-identical on this machine.
The libtcod 64-drop batched representative call took 127.16 ms; no cancellation
deadline or cross-platform repeatability was measured. Python high-water RSS
was 68,388 KiB including imports and all cases, without a matched native
solver-memory measurement. Current native hydraulic and libtcod surfaces differ
by 4.9859 units on average and 302.25 at maximum, reinforcing that timings do
not compare equal output quality. The combined maximum slope is much lower
than the historical combined result, but hydraulic-only outliers and visual
acceptance still require review.

This closes the isolated representative numerical comparison gap, while leaving
visual terrain acceptance, cancellation-latency bounds, shipping dependency
qualification and a comparable upstream thermal model open. Retain before/after
wide and close images under identical camera/lighting before selecting a new
production solver. Continue publishing only successfully completed Final fields.
