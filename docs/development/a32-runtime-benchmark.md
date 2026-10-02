# A32 runtime benchmark

The opt-in `a32_runtime_benchmark` target measures persistent A32 execution
without turning performance into a CI pass/fail threshold.

Configure a Release build with benchmarks enabled:

```sh
cmake -S . -B build-bench -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DLIBA32ANDROID_BUILD_TESTS=OFF \
  -DLIBA32ANDROID_BUILD_BENCHMARKS=ON
cmake --build build-bench --target a32_runtime_benchmark -j8
```

Run the default workload:

```sh
./build-bench/a32_runtime_benchmark
```

The optional first argument is the instruction budget per sample and the
second is the sample count. Defaults are 5,000,000 instructions and 5 samples.

For same-host comparisons, pin the process to one CPU when the platform
supports it:

```sh
taskset -c 0 ./build-bench/a32_runtime_benchmark 5000000 5
```

The benchmark performs a short warmup and reports the median
nanoseconds/instruction and MIPS for two workloads:

- `compute`: a persistent Dynarmic A32 executor running an add/branch loop;
- `fastmem`: a persistent executor running load/add/store/branch against
  `MappedGuestMemory`, and requiring fastmem to remain enabled.

Performance comparisons must use the same source revision, compiler family,
build type, parallel build level, benchmark arguments, and host power/load
conditions. Record binary size alongside runtime data when evaluating compiler
or linker changes.

This target is a measurement seam, not a compatibility test and not a
representative application workload. Do not use it as a PGO training corpus
or to justify architecture-specific compiler flags by itself.
