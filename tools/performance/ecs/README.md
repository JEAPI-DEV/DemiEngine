# ECS storage and mutation evaluation

This isolated prototype compares EnTT 3.15.0 with the former production
per-entity `std::any` map. `MapComponentStorage.h` preserves that historical
control only for this benchmark; it is never linked into the engine.
The fixture uses the engine's real Transform3D and MeshRenderer types.

Both backends run with full scanning and with explicit mutation notifications.
This separates storage layout from the benefit of avoiding unchanged work.
Changes are included in the timing; moving every object does not get a free
notification pass. Stable authoring identities are data, not EnTT handle values.

Build and run independently of native graphics and the engine build:

```sh
cmake -S tools/performance/ecs -B build/ecs-evaluation -DCMAKE_BUILD_TYPE=Release
cmake --build build/ecs-evaluation
ctest --test-dir build/ecs-evaluation --output-on-failure
python3 tools/performance/ecs/run.py \
  build/ecs-evaluation/demi-ecs-evaluation /tmp/demi-ecs-results.jsonl
```

The standalone build requires an installed nlohmann_json CMake package and
fetches a SHA256-pinned EnTT release. EnTT uses the MIT license; its license is
retained in the fetched source. Nothing installs another engine or modifies a
project. Release optimization is required for meaningful performance results.

The runner executes controls sequentially, alternating their order over three
rounds. Counts are 25,000 and 100,000, with static, 1%-moving, all-moving, and
1%-component-toggle workloads. Ten warmup frames precede sixty measured frames.
It rejects any divergence in cumulative visible-output checksums. The contract
check also compares every projected record after coalesced edits, component
removal/re-addition and entity destruction/recreation.

Every backend uses the same five synthetic culling volumes. These do not
implement the engine's full frustum, cascaded-shadow, material, hierarchy or GPU
pipeline. Report these as isolated CPU timings, never as game frame times or an
integrated engine speedup. Linux peak RSS is captured with getrusage; other
platforms report zero rather than inventing comparable memory measurements.

The signal-driven prototype does not expose writable component pointers.
Production callers that still mutate through retained pointers need migration
before they can rely exclusively on update notifications. Adding EnTT alone
does not make those writes observable.
