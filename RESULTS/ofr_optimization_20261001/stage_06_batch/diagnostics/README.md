# Stage 06 short candidate experiments

The retained candidate is `batch_no_local`: force the unchecked packed-cursor helpers inline and apply four independent 8-byte or 16-byte OFork gates with SSE2. The cursor is passed by reference; the local cursor copy experiment is absent. Production source is frozen after merging this candidate. A separate formal seven-round matrix and SGX SIM measurements are required before final acceptance.

These native diagnostic runs use two warmups and three measured rounds, alternating Supple/OFRSupple, with stage profiling enabled and heap tracking disabled. All values below are medians in milliseconds, calculated separately for each column. Consequently median total need not equal the sum of the two column medians. They include the complete control-generation and application work; they do not include ECALL encryption/decryption.

| Retained case | Supple offline | OFR offline | Supple online | OFR online | Online ratio Supple/OFR | Supple total | OFR total |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| N=2^20, K=16, P=1/16, 8 B | 145.330 | 202.586 | 129.848 | 41.041 | 3.164 | 275.178 | 243.054 |
| N=2^22, K=16, P=1/16, 8 B | 641.894 | 1011.252 | 589.047 | 195.647 | 3.011 | 1230.786 | 1206.431 |
| N=2^20, K=64, P=1/64, 16 B | 209.093 | 278.822 | 209.896 | 101.243 | 2.073 | 421.384 | 379.814 |

Candidate decisions:

- `inline_only` forces `nextUnchecked` and `nextFourUnchecked` inline. Its OFR online medians are 84.813, 393.288, and 142.527 ms for the three cases above. It does not establish an independent improvement over stage 05. The attributes remain in the retained batch combination to expose packet decoding to the compiler.
- `local_stream` also copies the cursor at contiguous-run entry and writes it back on exit. Online medians are 85.301, 394.381, and 153.449 ms. The 16-byte result regresses 7.66% relative to `inline_only`, so this change is removed.
- `batch` combines batch gates with the local cursor copy. It was compiled but not measured after the local-copy regression became clear. It is superseded, with no performance conclusion attributed to it.
- `batch_no_local` combines inline cursor helpers and batch gates without the copy. The table above supports retention pending formal testing.

The default `CONFIG.h` defines `COUNT_OSWAPS`. Both methods retain this build configuration. Four-gate packets increment the same logical primitive counter by four, and scalar tails continue to increment once per gate. This aggregates counter updates and may contribute to the observed gain along with SIMD routing and decoding. A counter-off attribution experiment was intentionally skipped: it would require a complete isolated configuration across all translation units, since this project's configuration has no include guard. The measurements do not establish a counter-off speedup or a hardware SGX speedup.

Validation of the retained candidate:

- Full OFR ASan/UBSan regression passes: 250952 exact routing cases, 28866 normalization/capacity cases, 38016 fused routings, 1040 tiled routings, four narrow-counter boundary routings, and six membership byte-overflow rejections. Leak detection is disabled for the sanitizer execution; address/undefined-behavior instrumentation is enabled.
- Production ASM regression passes 131072 four-gate packets and 4096 mixed postorder/DFS cases, including unaligned data, packed view offsets, and sentinels. Counter assertions verify postorder counts of 12, 32, and 80 gates for N=8, 16, and 32 respectively. The earlier estimate of 8192 mixed cases is replaced by the actual fixture's 4096 cases.
- The core compiles with the SGX SDK's `-nostdinc -nostdinc++` flags. The new header uses GNU vector types and builtins with SDK `<cstdint>/<cstring>`, without host intrinsic headers. This is a compilation check, not an SGX runtime measurement.

Raw CSV/logs are in the candidate subdirectories. `short_candidate_sources.tar.gz` contains every candidate's source, including the unmeasured `batch`. `production_core.sha256` identifies the three production files merged from the retained candidate, with the temporary absolute `oasm_lib.h` include restored to the production relative path. The parent experiment runner archives the final production source and runs the formal native/SIM matrix separately.

Diagnostic compile and run commands (substitute `CANDIDATE` with the candidate directory):

```sh
g++ -std=c++11 -O3 -DNDEBUG -DSUPPLE_MEMORY_TRACKING \
  -I/opt/intel/sgxsdk/include -ICANDIDATE \
  -ffunction-sections -fdata-sections \
  tests/ofrsupple/benchmark_vs_swo.cpp CANDIDATE/OFR.cpp \
  Enclave/SubSample_v2/OFR/helper.cpp Enclave/MemoryProfile.cpp \
  /tmp/ofr-writer-phase.w2gCPt/primitives.o \
  -Wl,--gc-sections -Wl,-z,noexecstack -pthread \
  -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free \
  -Wl,--wrap=memalign,--wrap=posix_memalign,--wrap=aligned_alloc \
  -o CANDIDATE/benchmark
CANDIDATE/benchmark 3 --warmup 2 --profile --case 1048576 65536 16 8
CANDIDATE/benchmark 3 --warmup 2 --profile --case 4194304 262144 16 8
CANDIDATE/benchmark 3 --warmup 2 --profile --case 1048576 16384 64 16
```

The temporary primitive object comes from the existing isolated host harness. Use the maintained stage runner for durable rebuilding; the commands above record the diagnostic setup rather than a distributable build procedure.

SDK compilation check, exit status zero:

```sh
g++ -m64 -maes -msse2 -O3 -g -nostdinc -nostdinc++ -fvisibility=hidden \
  -fpie -fstack-protector -std=c++11 -DSUPPLE_MEMORY_TRACKING \
  -IInclude -IEnclave -ICANDIDATE -I/opt/intel/sgxsdk/include \
  -I/opt/intel/sgxsdk/include/libcxx -I/opt/intel/sgxsdk/include/tlibc \
  -I/opt/intel/sgxsdk/include/stlport -I/opt/intel/sgxssl/include \
  -c CANDIDATE/OFR.cpp -o CANDIDATE/OFR_sgx.o
```
