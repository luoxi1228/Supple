#!/usr/bin/env bash
set -euo pipefail
bench_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
bench_build=$(mktemp -d /tmp/supple-ofrsupple-bench.XXXXXX)
nasm -f elf64 "$bench_dir/../../Enclave/oblivious_functions.asm" -o "$bench_build/primitives.o"
"${CXX:-g++}" -std=c++11 -O3 -DNDEBUG \
  -I"${SGX_SDK:-/opt/intel/sgxsdk}/include" \
  -ffunction-sections -fdata-sections \
  "$bench_dir/benchmark_vs_swo.cpp" \
  "$bench_dir/../../Enclave/SubSample_v2/OFR/OFR.cpp" \
  "$bench_dir/../../Enclave/SubSample_v2/OFR/helper.cpp" \
  "$bench_build/primitives.o" \
  -Wl,--gc-sections -Wl,-z,noexecstack -pthread \
  -o "$bench_build/benchmark_vs_swo"
"$bench_build/benchmark_vs_swo" "${1:-7}"
