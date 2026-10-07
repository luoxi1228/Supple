# FFOS rename validation

The rename preserves modes 4 and 5 and the serial selection-scratch
lifetime fix. The parallel sampler and mode 14 were removed. Validation
artifacts are under `/tmp/ffos-rename-validation`.

- FFOS_C: 2,952 routing cases pass with ASan, UBSan and leak detection.
- FFOS_FR: 1,843 routing cases, 2,304 tag cases and 10 invalid plans pass
  with ASan, UBSan and leak detection. The legacy-result comparison test passes.
- Memory tracker scenarios and seven Python CSV regressions pass, including
  explicit filenames for modes 4 and 5 and both offline profiles.
- SGX SIM: regenerated bridges, real AES-GCM sampling, invalid inputs,
  repeated-ECALL heap stability, application CSVs and offline profiles pass.
  Serial smoke cases pass; the application rejects removed mode 14.
- SGX HW: enclave, generated bridge, host library and applications compile
  and link. Hardware execution was not part of this validation.
- A recorded byte-control baseline restores and runs with the current native
  and SIM harnesses. Comparisons accept real archived labels and current FFOS
  labels without rewriting the inputs.
- SHA-256 checks confirm identical contents for the two migrated top-level
  CSVs and all tracked historical results. The existing production config
  and signed enclave remain identical to their pre-rename working copies.

Project-level `SUPPLE_*` profiling macros and generic SWO/OFR metric names
remain unchanged. Old sampler APIs appear only in the test-side archive
adapter and label normalization; production interfaces use FFOS names.
