# CompactionBasedSWO validation

This report and its raw CSV preserve the mode numbers used at measurement
time: old 9/10 are now 1/3, and old 2/6/8 are now 2/4/5. Current test and
benchmark commands use the new numbers.

Validated on 2026-10-01 with Intel SGX SDK simulation and an Intel Xeon
Platinum 8369B host. Production enclave and the test overlay use `-O3`.

## Correctness and integration

- `bash tests/compaction/run_tests.sh`: 1,056 deterministic cases against the
  real `markGen` body and `TightCompact_v2` dispatch. Each sample is checked
  against the selected IDs in the current input order, including full record
  contents, uniqueness, and preservation of all input records across K rounds.
  Cases cover N=1, powers/non-powers of two, RNG refill, M=1/M=N, K=1/2/4/8,
  and widths 4/8/12/16/24/32/40/64. Tests assert one reused flag buffer,
  exactly K compactions, zero shuffles, `2*K+1` clock calls, timing sums and
  exchange count deltas. Allocation failure injection covers flags and prefix
  counts, including failure after earlier samples succeeded. Adapter checks
  ensure cleanup and no encryption of incomplete samples.
- `bash tests/memory/run_tests.sh`: exact-byte memory tracker and CSV tests,
  including mode 10 parsing, invalid/missing measurement rejection, units,
  legacy backups, and overwrite behavior.
- `bash tests/memory/run_sgx_tests.sh`: real AES-GCM/PRB and ECALL tests for
  modes 2/6/8/9/10, repeated rounds, all three M*K vs N relationships,
  non-power-of-two inputs, invalid requests and authentication failure.
  Additional mode 10 cases cover all eight widths, N=1, M=1/M=N, overflow
  and a 4 GiB output allocation in a 64 MiB enclave. Invalid measurements
  are rejected and allocation failure leaves encrypted output untouched.
  End-to-end application/script tests verify headers, measured bytes to MiB,
  old-file backups, appending to new files, warmups and K=0 rejection.
- Existing SWO serial: 2,952 cases; SWO parallel/fallback: 18 cases;
  FFOS_FR: 1,840 cases. All passed.
- The native HW enclave, host library and application build and sign
  successfully. No SGX device is exposed on this host, so hardware execution
  has not been tested.

Host tests use AddressSanitizer and UndefinedBehaviorSanitizer. LeakSanitizer
cannot run under this environment's tracing; it is disabled for these runs.
The focused allocation adapter separately checks that array allocations and
random-pool lifetimes return to zero, including exception paths.

## Sampling baseline performance

`n=16384, m=256, k=64, block_size=16`. Each setting uses a fresh enclave,
two warmups and five measured rounds, repeated for five trials. Every output
is decrypted and checked for record integrity and per-sample uniqueness.
The table uses memory tracking enabled and reports the median of each
trial's mean time. ECALL includes decryption/encryption; algorithm time
excludes them. Memory is the maximum measured allocation peak.

| Mode | ECALL ms | Algorithm ms | Heap peak bytes |
| --- | ---: | ---: | ---: |
| 9: ShuffleBasedSWO | 376.812 | 302.338 | 804828 |
| 10: CompactionBasedSWO | 109.329 | 35.164 | 706524 |

For this configuration, mode 10 takes approximately 29.0% of mode 9's ECALL
time and 11.6% of its algorithm time. Mode 10 reports cumulative mark
generation and compaction/copy time and performs no output shuffle; output
order therefore differs from the shuffle baseline. These are SIM results
for this configuration, not a hardware performance guarantee.

Raw rows for both baselines, with tracking enabled and disabled, are in
`benchmark_sgx_sim.csv`. Reproduce using
`bash tests/memory/run_sgx_tests.sh --benchmark`; filter its numeric rows to
current modes 1/3 and tracking=1. Integration build logs and full benchmark rows
from this run are retained under
`/tmp/supple-compaction-integration-s9oux_4z`.
