# A32 compiler/linker optimization evidence — 2026-10-02

## Scope

Issue #54 evaluated post-structural compiler tuning on main revision
`52742c665f1355e49fe62954e62c30d8b2f59f1e`.

Measurement host:

- Fedora Linux;
- Intel Xeon E5-2698 v4, 40 logical CPUs;
- 15 GiB RAM;
- GCC/G++ 16.2.1;
- CMake 4.3.0 and Ninja 1.13.2;
- Android NDK r27d (`$ANDROID_HOME/ndk/27.3.13750724`).

Build timing used a clean Release tree, system GCC/G++, tests disabled,
no ccache, `liba32android` target only, and fixed `-j8`.

Baseline results:

| Metric | Baseline |
| --- | ---: |
| Clean library build | 67.19 s |
| Peak build RSS | 963,356 KiB |
| `liba32android.so` size | 8,704,144 bytes |
| Long-run compute | 126.8283 ns/instruction, 7.8847 MIPS |
| Long-run fastmem | 133.3281 ns/instruction, 7.5003 MIPS |

Runtime comparisons used the persistent-executor benchmark, CPU 0 affinity,
5,000,000 instructions per sample, and median of 5 samples. Each candidate
was compared with a fresh paired baseline run in the same session.

## Target-scoped IPO/LTO

CMake IPO was enabled only on `liba32android`; Dynarmic compile commands
remained free of LTO flags.

| Metric | Baseline | IPO | Delta |
| --- | ---: | ---: | ---: |
| Clean build | 67.19 s | 71.66 s | +6.65% |
| Shared object | 8,704,144 B | 8,703,328 B | -816 B (-0.009%) |
| Compute | 126.5886 ns/i | 126.6492 ns/i | +0.048% slower |
| Fastmem | 132.6658 ns/i | 132.8168 ns/i | +0.114% slower |

Decision: reject. Build cost is material while size and runtime effects are
negligible or slightly negative.

## `-fno-semantic-interposition`

The flag was applied only to liba32android C++ objects; Dynarmic remained
unchanged.

| Metric | Baseline | Candidate | Delta |
| --- | ---: | ---: | ---: |
| Clean build | 67.19 s | 66.93 s | -0.39% |
| Shared object | 8,704,144 B | 8,729,256 B | +25,112 B (+0.29%) |
| Compute | 126.9076 ns/i | 125.6917 ns/i | -0.958% faster |
| Fastmem | 132.1924 ns/i | 133.0221 ns/i | +0.628% slower |

Decision: reject. The result is mixed, increases binary size, and does not
show a consistent runtime improvement across the measured hot paths.

## PGO and architecture-specific flags

PGO is not enabled. The current benchmark is intentionally a small persistent
A32 execution microbenchmark, not a representative application workload.
Training a profile on it would optimize the synthetic benchmark rather than
establish portable product benefit.

Host-specific ISA flags such as `-march=native` are excluded because they
violate the supported-host and Android arm64 portability requirement.

## Conclusion

No compiler/linker runtime flag evaluated in #54 clears the evidence bar.
The repository therefore keeps its existing Release flags unchanged and gains
an opt-in reproducible benchmark target for future measured evaluations.

## Validation

Final tracked branch validation on the same Fedora host:

- opt-in Release benchmark target built and executed successfully;
- tracked benchmark at 5,000,000 instructions / 5 samples on CPU 0:
  compute 128.5813 ns/instruction (7.7772 MIPS), fastmem 132.0848
  ns/instruction (7.5709 MIPS);
- full host CTest: 68/68 passed;
- Android NDK r27d cross-build: arm64-v8a, API 26, diagnostics enabled,
  Dynarmic no-execute support enabled;
- Android runtime plus both diagnostic probes built successfully;
- all checked Android LOAD segments retained 16 KiB (0x4000) alignment.

The measured conclusion is therefore to ship the benchmark/evidence seam and
leave Release compiler/linker flags unchanged.
