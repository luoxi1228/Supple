These native diagnostics compare OFR writer candidates at N=1048576,
M=65536, K=16 and 8-byte records. They are profiling runs, separate from the
formal seven-round matrix, and do not establish SGX HW performance.

`checkpoint_rounds.csv` preserves the full paired output for the final core
checkpoint; `joint_checkpoint_rounds.csv` repeats it after the final OFRSupple
tag/projection candidate was frozen using SGX-compatible GNU vector helpers.
The latter source snapshot is `joint_checkpoint_source.tar.gz`.
Earlier stdout medians are transcribed in `candidate_medians.csv`;
their phase logs are retained here. `sse4` used one cold measured round. All
other candidates used two warmups and three measured rounds, with profiling
enabled and runtime heap tracking disabled. OFRSupple and Supple were paired
by the existing alternating harness. Median total is calculated independently
from the sum of median control and median apply.

The original diagnostic sources are stored in `diagnostic_sources.tar.gz`.
Only copied sources in `/tmp` received per-stride `PHASE` instrumentation;
production OFR contains no such instrumentation. Phase rows include node
size, public stride, tile size, membership word width, routing flag and
elapsed milliseconds. They include warmup calls and recursive OFRSupple
nodes, so select the desired node size before aggregating them.

Candidate history:

- `sse4`: initial four-residue 32-bit SIMD writer.
- `sse8`: eight-residue 16-bit SIMD writer for short public node lengths.
- `group`: changed pass-two loop order to residue groups first; reverted
  after paired totals and writer time showed no improvement.
- `remaining`: group-first variant storing quota minus rank; reverted after
  paired totals showed no improvement.
- `sse16`: sixteen-residue 8-bit SIMD writer, with a simpler first statistics
  scan. Initial byte threshold was length 128.
- `prefix`: four-gate prefix scan on early public strides through 16;
  strides 8 and 16 regressed and were removed from the selection rule.
- `limits`: prefix scan only for strides through 4, byte counters through
  length 256, short counters through length 65536. Counter arithmetic uses
  unsigned modular adds; signed rank comparisons occur only before the
  possible terminal rank 128 or 32768.
- `checkpoint`: the retained limits candidate plus scalar owned-tag
  normalization avoiding a duplicate threshold comparison and conditional
  selections. The complete core regression and SGX SDK-only compilation
  passed before this checkpoint was frozen.

The final joint checkpoint remains slower in total than paired Supple at this
single native size. The main matrix, newly added maximal-rank correctness
cases, SGX SIM ECALL measurement, and target HW measurement are required
before final acceptance. The packed storage requirement is unchanged.
The joint total of 308.35 ms versus the preceding 310.73 ms is only a small
change in this three-round diagnostic; it does not establish a separate
integration speedup or satisfy the end-to-end performance target.

To rebuild a copied diagnostic source using the repository harness:

```sh
nasm -f elf64 Enclave/oblivious_functions.asm -o /tmp/ofr-primitives.o
g++ -std=c++11 -O3 -DNDEBUG -DSUPPLE_MEMORY_TRACKING \
  -I/opt/intel/sgxsdk/include -IEnclave/SubSample_v2/OFR \
  -ffunction-sections -fdata-sections \
  tests/ofrsupple/benchmark_vs_swo.cpp /tmp/OFR_checkpoint_diagnostic.cpp \
  Enclave/SubSample_v2/OFR/helper.cpp Enclave/MemoryProfile.cpp \
  /tmp/ofr-primitives.o -Wl,--gc-sections -Wl,-z,noexecstack -pthread \
  -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=memalign,--wrap=posix_memalign,--wrap=aligned_alloc \
  -o /tmp/ofr-diagnostic
/tmp/ofr-diagnostic 3 --warmup 2 --case 1048576 65536 16 8 --profile \
  > /tmp/ofr-rounds.csv 2> /tmp/ofr-phases.log
```

The copied diagnostic includes the original absolute repository path for
`oasm_lib.h`; adjust that include when reproducing in another checkout.
