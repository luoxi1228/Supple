#!/usr/bin/env bash
set -euo pipefail
test_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
test_build=$(mktemp -d /tmp/supple-ffos_fr-test.XXXXXX)
nasm -f elf64 "$test_dir/../../Enclave/oblivious_functions.asm" -o "$test_build/primitives.o"
"${CXX:-g++}" -std=c++11 -O1 -g -Wall -Wextra -Wno-unused-parameter \
  -Wno-unused-variable -Wno-unused-function \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"${SGX_SDK:-/opt/intel/sgxsdk}/include" \
  -ffunction-sections -fdata-sections \
  "$test_dir/ffos_fr_test.cpp" "$test_build/primitives.o" \
  -Wl,--gc-sections -Wl,-z,noexecstack -pthread -o "$test_build/ffos_fr_test"
"$test_build/ffos_fr_test"
"${CXX:-g++}" -std=c++11 -O2 -g -DOFR_TEST_GATE_COUNTS \
  -include "$test_dir/../../CONFIG.h" \
  -I"${SGX_SDK:-/opt/intel/sgxsdk}/include" \
  -ffunction-sections -fdata-sections \
  "$test_dir/gate_backend_test.cpp" \
  "$test_dir/../../Enclave/SubSample_v2/OFR/OFR.cpp" \
  "$test_dir/../../Enclave/SubSample_v2/OFR/helper.cpp" \
  "$test_build/primitives.o" -Wl,--gc-sections -Wl,-z,noexecstack -pthread \
  -o "$test_build/gate_backend_test"
"$test_build/gate_backend_test"
PYTHONDONTWRITEBYTECODE=1 python3 "$test_dir/test_legacy_results.py"
