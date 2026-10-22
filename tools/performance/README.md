# Isolated component-storage benchmark

`component_storage_benchmark.cpp` compares the former cached-map `ComponentStorage`
with the historical uncached RTTI-key map. The map control is now preserved
only for historical comparisons; see `ecs/` for the EnTT migration evaluation. Both retain `std::type_index` equality
and `std::any_cast`; the historical implementation exists only as this benchmark's
control. No engine library, renderer, GPU, dependency installation or project data
is needed.

From the repository root:

```sh
c++ -std=c++20 -O3 -DNDEBUG -Isrc tools/performance/component_storage_benchmark.cpp -o /tmp/demi-component-storage-benchmark
taskset -c 4 /tmp/demi-component-storage-benchmark 120 > /tmp/demi-component-storage.csv
```

Choose an available CPU for `taskset`, or omit affinity when it is unavailable.
Run without simultaneous builds or engine processes. Record the compiler, build
flags and CPU alongside results. The optional argument sets scans per timed sample;
the default is 120.

The fixture contains 23,848 independent storage objects. All have native
Transform3D components, 97% have MeshRenderers, 25% have sphere colliders, and
small disjoint groups have camera or light components. One has Terrain3D, and
none has Transform2D. Workloads perform two common-hit probes, eight mixed probes,
or six mostly-missing probes per object. Five samples alternate implementation
order after warmup, and every sample checks matching value-derived checksums.

Output includes milliseconds per complete fixture scan and population allocation
calls/bytes. Allocation bytes are cumulative requested bytes while constructing
this fixture, not RSS or a peak-residency measurement. The benchmark replaces the
standalone process's ordinary allocation functions solely to count those requests.
Use medians; the result isolates component lookup cost and does not establish an
end-to-end editor or game speedup.
