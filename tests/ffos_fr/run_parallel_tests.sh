#!/usr/bin/env bash
set -euo pipefail
test_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
test_build=$(mktemp -d /tmp/supple-par-test.XXXXXX)
nasm -f elf64 "$test_dir/../../Enclave/oblivious_functions.asm" -o "$test_build/primitives.o"
flags=(-include "$test_dir/../../CONFIG.h" -DFFOS_FR_EXTERNAL_OFR -std=c++11 -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function -ffunction-sections -fdata-sections -I"${SGX_SDK:-/opt/intel/sgxsdk}/include")
case "${SANITIZER:-address,undefined}" in
  none) ;;
  *) flags+=(-fsanitize="${SANITIZER:-address,undefined}" -fno-omit-frame-pointer) ;;
esac
"${CXX:-g++}" "${flags[@]}" "$test_dir/parallel_test.cpp" "$test_dir/../../Enclave/SubSample_v2/OFR/OFR.cpp" "$test_dir/../../Enclave/SubSample_v2/OFR/helper.cpp" "$test_build/primitives.o" -Wl,--gc-sections -Wl,-z,noexecstack -pthread -o "$test_build/parallel_test"
"$test_build/parallel_test"
echo "Build: $test_build"

"${CXX:-g++}" -std=c++11 -O2 "$test_dir/cpu_budget_test.cpp" -o "$test_build/cpu_budget_test"
"$test_build/cpu_budget_test"
"${CXX:-g++}" -std=c++11 -O2 "$test_dir/pool_counter_test.cpp" -pthread -o "$test_build/pool_counter_test"
"$test_build/pool_counter_test"
PYTHONDONTWRITEBYTECODE=1 python3 "$test_dir/test_cpu_budget.py"
