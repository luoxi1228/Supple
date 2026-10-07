# FFOS-FR scalar / Opt mode split

Validated on 2026-10-07. Logs and isolated build metadata are in
`/tmp/ffos-mode-split-validation`.

| Mode | Code/result name | Online gate backend |
| --- | --- | --- |
| 5 | FFOS_FR | One assembly OFork call and one counter update per gate |
| 6 | FFOS_FR_Opt | Existing four-gate SSE2 for balanced postorder B=8/16 spans |

Modes 1–5 now share the per-gate OSWAP/OFork execution and counter-update
policy. This does not equate OSWAP's single flag with OFork's two independent
flags, or remove the algorithms' different control generation and copying.
Other record widths and non-postorder FR routes retain their scalar fallback.

The FR variants share the packed control tape, membership processing,
prepared shapes, workspaces, preflight validation, timers and memory ownership.
The backend is an explicit public call parameter, so interleaved mode 5/6
calls do not change any persistent global backend setting. Low-level OFR
APIs retain their existing optimized default for unrelated primitive tools.

The optimized implementation has its own `FFOS_FR_Opt` source directory,
header, EDL and `DecFFOS_FR_Opt` ECALL. Trusted and untrusted bridges were
regenerated with edger8r. CLI, heap estimation, CSV/profile output and both
native/SIM benchmark harnesses support all three FFOS methods. Direct
experiments default to modes 1–6; other experiment dimensions, repeat/warmup
defaults and the existing batch runner's method selection remain unchanged.

## Checks

- 1,843 routing cases compare scalar and Opt output; 2,304 one-word tag cases
  and 10 invalid-plan preflight cases pass under ASan/UBSan. LeakSanitizer is
  disabled for the rerun because it cannot operate under this environment's
  ptrace supervision; no LSan result is claimed.
- 224 real assembly/SSE2 cases cover all four gate controls, multiple packed
  offsets, 4/8/12/16/24/32/64/256-byte widths, boundary guards and identical
  output. Test-only execution probes confirm zero four-gate batches for scalar
  entrypoints, positive batches for eligible Opt entrypoints, and identical
  logical gate counts. Both ECALL wrappers are exercised with the real OFR TU.
  Probe code is absent from normal production builds.
- Seven Python experiment regressions pass, including explicit mode 6 result
  and offline-profile filenames, CLI arguments, heap sizing and CSV backup
  behavior. The comparison regression accepts legacy labels plus separate
  scalar/Opt rows, without changing input files.
- Isolated SGX SIM integration passes for modes 1–6: real AES-GCM
  authentication, record identity/payload, sample uniqueness, invalid requests,
  repeated calls, allocation failures and actual application-generated CSVs.
  Interleaved scalar/Opt calls at B=8/16/24 report equal heap peaks.
- Three-method native and paired SIM benchmarks run at N=65536, M=1024,
  K=64, B=16. The native harness now includes CONFIG.h in every TU, correcting
  its previous asymmetric COUNT_OSWAPS configuration.
- HW enclave, bridge and application compilation succeeds in the isolated
  checkout. No SGX hardware device is available for a HW runtime test.
- The historical stage_00_baseline source is restored and built with the
  current test-side adapter; its native two-method benchmark passes.
- Production Enclave.config.xml, both signed enclaves and both shared-library
  copies have identical before/after SHA-256 values. Validation used temporary
  signatures and configurations exclusively.

## Result migration

Existing optimized `RESULTS/FFOS_FR.csv` was moved without content changes to
`RESULTS/FFOS_FR_Opt.csv`. SHA-256 before and after:

`61c5d35d8334c21c1234e563a3588dd412d50a29f1413bf6f0e44d94061e2dc5`

No top-level FR offline-profile file existed to migrate. Fresh scalar mode 5
results will create `FFOS_FR.csv`; mode 6 writes `FFOS_FR_Opt.csv`. Diagnostic
filenames follow the same names with `_offline_profile.csv`. Archived result
directories and source snapshots were left unchanged.
