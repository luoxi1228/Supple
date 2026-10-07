# FFOS-FR output-buffer recursion

Validated on 2026-10-07. Raw logs, snapshots, binaries and per-round CSVs are
under `/tmp/ffos-fr-inplace-validation` for this session.

Prepared exact-fit subtrees (`n == m*k`) copy the input into their final output
slice once, then route and recurse in that allocation. Child slices are
disjoint and already occupy their final sample positions. This removes
per-depth payload workspaces and leaf output copies for those subtrees.
Disjoint inputs remain unchanged. Frontier filtering and raw DFS control tapes
retain their existing readers; prepared exact-fit subtrees below a filtered
node also use the new path. Both FR backends share this storage optimization.

The OFork implementation, control generation, packed tape, gate ordering,
backend selection and per-gate counting are unchanged. Mode 5 still calls
scalar assembly OFork once per logical gate. Mode 6 retains its SSE2 backend.
Whole-plan validation still runs before any output write.

For K=64 and M*K=N, explicit record-copy volume falls from `7*N*B` to `N*B`.
At N=1,048,576 and B=16 this is 112 MiB versus 16 MiB. This is cumulative
copy volume, not peak heap or physical DRAM traffic. The initial copy remains
inside the online timer; no work moves into the offline phase.

## Native measurements

The unchanged production native harness uses the same deterministic membership
input, -O3, COUNT_OSWAPS enabled in every translation unit, two warmups and
seven measured rounds. It rotates C/FR/Opt execution order each round. The
before/after snapshots alternate execution order between input sizes. These
are independent method medians, not a claim about every paired round. The
native apply timer includes output allocation/destruction in both snapshots;
the application ECALL timer allocates the output before timing in both.

Parameters: B=16, K=64, M=N/64. Times are milliseconds.

| N | Before C | Before FR | After C | After FR | FR time reduction | After C/FR |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 65,536 | 13.512 | 6.894 | 13.566 | 6.229 | 9.64% | 2.178 |
| 262,144 | 59.853 | 30.787 | 58.546 | 27.915 | 9.33% | 2.097 |
| 1,048,576 | 273.259 | 140.718 | 266.657 | 126.119 | 10.37% | 2.114 |
| 4,194,304 | 1250.976 | 656.293 | 1243.908 | 580.507 | 11.55% | 2.143 |

Heap collection ran separately with three measured rounds and one warmup.
At N=1,048,576, B=16, K=64, the native control/apply allocation peak changes
from 60.6276 MiB to 38.5983 MiB for both FR variants; C stays at 54.0002 MiB.
This scope excludes the preexisting root membership and input allocations.
Offline allocations can become the peak after the online buffers disappear.
Native timings and allocation scopes are not HW SGX measurements.

The isolated SIM application also ran mode 5 at N=1,048,576, B=16, K=64
with three measured calls after one warmup. Its full algorithm allocation
peak is 57,350,824 bytes (54.6940 MiB), and its online gate count remains
55,050,240. This uses the ECALL allocation scope rather than the native
control/apply scope above. SIM timing is not used as a HW speedup estimate.

Reproduce a current native case with:

```sh
bash tests/ffos_fr/run_benchmark.sh 7 --warmup 2 --case 1048576 16384 64 16
```

## Verification

- ASan/UBSan: 1,843 integration cases, 2,304 tag cases, 48 new guarded output
  view cases and 10 invalid-plan preflight cases pass. The new cases compare
  byte-for-byte with the independent raw DFS reader, preserve the input and
  unused output capacity, and cover shifted packed controls, odd/asymmetric
  shapes and multi-word membership. LeakSanitizer is disabled because this
  environment supervises processes through ptrace; no LSan result is claimed.
- 224 real assembly/SSE2 cases and the API/ECALL backend probes pass. Scalar
  APIs execute zero SSE2 batches; eligible Opt APIs retain their batches;
  logical gate counts are unchanged, and the original input stays intact.
- Memory tracker and seven experiment/CSV regressions pass.
- Isolated SGX SIM integration passes for all six modes, real AES-GCM,
  record payload/identity, sample uniqueness, repeated/interleaved calls,
  invalid requests, allocation failures and actual generated result/profile
  filenames. Temporary signatures and configurations are used throughout.
- Isolated HW enclave, untrusted bridge and application compilation succeeds. No SGX HW
  device is available here; target-machine timing remains to be measured.
- Before/after SHA-256 checks confirm that production enclave configuration,
  signed enclaves, shared libraries, the experiment runner, existing top-level
  result CSVs, OFR.cpp and the OSwap/OFork assembly templates are unchanged.

The production signed binaries were deliberately left at their existing
versions. Rebuild before collecting new HW results. Existing result rows
describe the previous implementation and were not replaced or reclassified.
