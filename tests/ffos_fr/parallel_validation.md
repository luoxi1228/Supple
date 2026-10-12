# FFOS_FR_Par validation

Mode 6 now uses a CPU-scalable pool for offline control generation and online
application. Mode 5 retains the serial strategy. No fixed thread ceiling is set.

Passed checks:

- ASan/UBSan routing regression: 1,843 FR cases, 2,304 single-word tag cases,
  48 guarded output-view cases and 10 plans rejected before writes.
- Real assembly/SSE2 backend equivalence: 224 cases; scalar mode remains scalar.
- Real pthread pool and OFR/SSE2 parallel regression: fixed membership,
  identical tapes, all metadata/shapes and complete ordered output at
  1/2/3/9/17/32/64/auto budgets. Covers MK<N, MK=N, MK>N, odd branches,
  multiword membership, shared packed-byte boundaries and 4/8/16/24/40 B records.
- Partial thread creation failure (after 0/1/5 successful workers), parent and
  worker exceptions, failed algorithm tasks, repeated pools and nested OSWAP
  reductions. An ECALL pool creation failure preserves the result buffer.
- CPU-probe mocks in Python/C++: affinity, quota ceiling, cgroup v1/v2 ancestors,
  fallback and 16/32/64-core environments. Production make rules rebuild on the
  first changed-capacity invocation and avoid recompilation when unchanged.
- Tracker regression: simultaneous allocations from 32 workers, nested allocator
  entry, full release across repeated rounds, realloc and failure accounting.
- FFOS_C regression: 2,952 cases. OFR regression: 250,952 exact routings,
  28,866 normalization/capacity cases, 38,016 fused word routes, 1,040 tiled routes,
  plus real production four-gate and mixed postorder/DFS checks.
- SGX SIM: authenticated ECALL output, sample identity/uniqueness, repeated
  memory measurements, mode-6 1/3/auto budgets, MK<N/=N/>N and record widths
  4/8/16/24/40/256 B. Real Application/CSV/profile parsing also passes.
- Real default make/sign flow: capacity 8→1→8. A budget of 3 against capacity 1
  is rejected with a rebuild/sign instruction. After one auto rebuild, 8 threads
  execute. Affinity 0–2 selects 3 threads; requesting 9 against 8 CPUs is rejected.

ASan/UBSan runs use `ASAN_OPTIONS=detect_leaks=0` because this environment runs
processes under a tracer. Allocation ledger release tests pass. A separate TSan
binary was compiled and attempted with default PIE and with PIE disabled; both
failed before tests with `ThreadSanitizer: unexpected memory mapping`.
[Recorded failure](../../RESULTS/ffos_fr_parallel/tsan.log). TSan is **not passed**.
No SGX hardware device is exposed; HW/EPC scaling remains to be measured there.

## Native scaling measurement

Current allowance: 8 logical CPUs. N=65,536, M=1,024, K=64, record width=16 B;
2 warmups and 7 measured rounds per budget. Values below are per-round medians
in milliseconds. Total is the median of paired control+apply times, so it can
differ from the sum of independent medians. Fixed membership excludes Mark;
control includes pool startup and apply includes pool shutdown. Heap tracking
and phase profiling were disabled for this timing run. Par one-thread is the
SIMD baseline. These host results do not establish SGX hardware scaling.

| Threads | Control ms | Apply ms | Total ms | Speedup vs Par 1 |
| --- | --- | --- | --- | --- |
| 1 | 13.190624 | 4.188018 | 17.365757 | 1.00x |
| 2 | 8.359717 | 2.589757 | 10.954926 | 1.59x |
| 4 | 6.513388 | 1.990224 | 8.503319 | 2.04x |
| 8 | 7.399583 | 2.260361 | 9.912075 | 1.75x |

[Raw Par rows](../../RESULTS/ffos_fr_parallel/FFOS_FR_Par.csv) and
[environment/source digest](../../RESULTS/ffos_fr_parallel/metadata.json).
Each `threads_*.csv` also retains the paired C and serial FR rows.
The 8-thread run is slower than 4 threads on this workload; automatic CPU
allocation selects a resource budget, rather than predicting an optimal budget.

Reproduce with:

```
ASAN_OPTIONS=detect_leaks=0 bash tests/ffos_fr/run_parallel_tests.sh
ASAN_OPTIONS=detect_leaks=0 bash tests/ffos_fr/run_tests.sh
bash tests/memory/run_tests.sh
python3 tests/ffos_fr/run_parallel_benchmark.py --backend native --rounds 7 --warmup 2 --output /tmp/par-scaling
python3 tests/ffos_fr/run_parallel_benchmark.py --backend sim --memory --output /tmp/par-sim
SANITIZER=thread bash tests/ffos_fr/run_parallel_tests.sh
```
