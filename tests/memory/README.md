# Algorithm heap peak regression tests

Run `bash tests/memory/run_tests.sh` for exact-byte tracker tests and CSV
parsing/migration tests. Allocator adapters let the tests force failed and
moving reallocations; explicit host new/delete adapters exercise STL buffer
overlap without depending on allocation calls inside a dynamically linked
host standard library. Production uses the SGX static C++ runtime.

Run `bash tests/memory/run_sgx_tests.sh` from an SDK/SGX SIM environment for
real ECALL, AES-GCM, PRB, Shuffle, CompactionBasedSWO, FFOS_C, FFOS_FR and FFOS_FR_Opt integration. The build
uses the existing test-only PIC/TLS overlay and creates the signed enclave,
host library and executables under `/tmp`; it does not replace the tracked
application binaries or result CSVs. All six modes are tested with
`k=1`, `m*k<n`, `m*k=n`, `m*k>n`, repeated rounds, invalid calls and
PSQF's non-divisible fallback, and failed AES-GCM authentication. Mode 3 also
tests specialized/generic record widths, N=1, M=1/M=N, overflow and failed
output allocation. Production enclave objects are rebuilt with
the SIM overlay; rebuild with the production Makefile before hardware runs.

FFOS_C, FFOS_FR and FFOS_FR_Opt must report identical peaks across repeated identical
ECALLs under `TCSPolicy=1`. Their selection scratch is owned by each control
generation/apply call, shared across serial recursive compactions, and freed
on return or exception; it does not depend on TLS surviving the next ECALL.

Run `bash tests/memory/run_sgx_tests.sh --benchmark` to report five alternating
enabled/disabled trials per mode at `n=16384,m=256,k=64,block_size=16`, with
two warmups and five measured rounds per trial. Each setting uses a fresh
enclave and validates every output, so disabled runs cannot establish
untracked scratch for an enabled measurement. Compare medians of the mean
ECALL/algorithm times for each trial, rather than individual noisy rounds.
Use `MEMORY_TRACKING=0` with a **full rebuild** of the enclave for a baseline
without allocation wrappers; this baseline supports direct test ECALLs with
collection disabled, while the normal application deliberately refuses to
write a result when requested measurements are unavailable.

To compare all three settings within one rotating sequence, pass
`--compare /absolute/path/to/unwrapped/enclave.signed.so` to the SGX test
script. Output uses `tracking=-1` for the unwrapped build, `0` for the wrapped
build with collection disabled, and `1` for collection enabled. The script
always fully rebuilds the instrumented SIM enclave. To prepare an unwrapped
enclave, run `MEMORY_TRACKING=0 bash tests/memory/run_sgx_tests.sh --baseline`
first and retain the signed artifact path printed on stderr.

The recorder covers requested allocation bytes, not allocator rounded usable
sizes. SGX dlrealloc frees `realloc(p,0)`, and allocates a moving destination
before releasing the old block; both behaviors are reflected in the ledger.
The supported experiment modes are serial; modes 1–6 enable this recorder.

The FR scalar (mode 5) and four-gate SSE2 (mode 6) paths are also compared
within the same enclave: both authenticate outputs and report identical heap
peaks across repeated calls at 8/16/24-byte widths and non-power-of-two sizes.
