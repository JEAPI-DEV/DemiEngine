# CPU Blast / Jolt feasibility probes

This is an opt-in integration experiment, not a shipped destruction subsystem or
a public engine API. Default engine builds do not download or link Blast.

## Dependency audit

- Source: NVIDIA-Omniverse/PhysX, commit
  `4f2103c3a9052906296defb12166753450ef787c`; `blast/VERSION.md` is 5.0.6.
  The source archive is SHA-256 verified by CMake; no full Git history is fetched.
- The repository name is not the dependency boundary: the explicit CMake source
  list compiles only Blast low-level and common sources, with bundled
  `NvFoundation` headers. It never invokes the upstream full SDK bootstrap.
- No PhysX, CUDA, Omniverse, GPU, toolkit, authoring, stress, or serialization
  extension is built. The Linux standalone executable links the standard C/C++
  runtime and math libraries only. Review the source list and binary linkage when
  updating the pin; `ldd` alone does not audit statically linked code.
- Source files and `blast/PACKAGE-LICENSES/blast-sdk-LICENSE.md` carry NVIDIA's
  BSD-3-Clause terms. Retain upstream notices in source and reproduce them with
  any distributed binary. The probe is not added to game packaging. Audit each
  additional extension and its dependencies before enabling it.
- Blast requires exactly one of `_DEBUG`/`NDEBUG`. Android also needs the supported
  `NV_C_EXPORT` override to restore C linkage: upstream's default platform guard
  excludes Android even though implementations use `extern "C"`.
- No serialized Blast blobs are persisted by this spike. Asset/family binary
  formats must be versioned and audited before they become cooked/save data.

The library's physics-independent architecture is described in the
[official SDK documentation](https://nvidia-omniverse.github.io/PhysX/blast/index.html).

## Run

Standalone, without building the engine or Jolt:

```sh
cmake -S tools/3d-feasibility -B build/blast-feasibility -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/blast-feasibility
ctest --test-dir build/blast-feasibility --output-on-failure
```

To reuse a verified checkout, pass
`-DFETCHCONTENT_SOURCE_DIR_DEMI_BLAST_SOURCE=/absolute/path/to/checkout` and verify
its commit matches the pin. Otherwise CMake fetches the pinned repository.

Including the bridge to Demi's existing `PhysicsWorld3D`/Jolt integration:

```sh
cmake --preset linux-debug -DDEMI_BUILD_3D_FEASIBILITY=ON
cmake --build --preset linux-debug --target demi-blast-probe demi-blast-jolt-probe
ctest --preset linux-debug -R 'demi-blast.*probe' --output-on-failure
```

The CPU fixture checks sub-threshold damage versus full separation of three
connected chunks, stable extracted IDs, and 100 creation/split/cleanup cycles.
The Jolt probe creates physical boxes from the detached IDs and verifies their
settling positions, mass-dependent impulse response, and floor collision over 20
world lifetimes.
It is a handoff proof for single-chunk actors, **not** an intact-body replacement,
compound-body split, mass/inertia conservation, or general fracture asset loader.

## Work-boundary measurements

`demi-blast-work-benchmark` extends the fixture to chains and 2D grids with 250,
500, 1,000, 2,000, and 4,096 chunks. It compares sub-threshold damage with complete
bond breakage, and 64-command batches with one full damage batch. Every case
verifies topology and chunk IDs. Two warmups precede ten recorded repetitions;
`--smoke` runs smaller correctness cases without timing assertions in CTest.

```sh
cmake --build build/blast-feasibility --target demi-blast-work-benchmark
./build/blast-feasibility/demi-blast-work-benchmark > build/blast-feasibility/work-benchmark.csv
```

CSV timing separates asset preparation, total/max damage-application call time,
and one actor split call. Family setup, split scratch allocation, result
extraction, and cleanup are outside `split_ms`. These are sampled maxima for
synthetic cases, not a worst-case execution-time proof or whole-frame budget.
Batches execute consecutively before splitting: this is not yet a cross-frame
scheduler or an interleaved damage policy.

`NvBlastActorApplyFracture` accepts bounded command arrays. `NvBlastActorSplit`
is measured as a synchronous whole-actor call; its scratch buffer is not a saved
continuation. Limiting damage commands does not also bound split cost. Asset
complexity, call/job boundaries, queue latency, and Jolt body-transition cost
must be budgeted separately.

## Android build feasibility

```sh
cmake -S tools/3d-feasibility -B build/blast-android-feasibility -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=/opt/android-sdk/ndk/29.0.14206865/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-23
cmake --build build/blast-android-feasibility
```

ARM64 cross-compilation is not Android runtime qualification. Run on a connected
physical device before enabling this in Android engine builds.
