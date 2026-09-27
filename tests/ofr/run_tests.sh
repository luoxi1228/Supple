#!/usr/bin/env bash
set -euo pipefail
test_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
test_build=$(mktemp -d /tmp/supple-ofr-test.XXXXXX)
echo "Test artifacts: $test_build"
"${CXX:-g++}" -std=c++11 -O1 -g -Wall -Wextra -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  "$test_dir/ofr_test.cpp" -o "$test_build/ofr_test"
"$test_build/ofr_test"

# Exercise the production OFork assembly specializations and its generic
# byte-wise fallback. This build intentionally does not define BEFTS_MODE.
"${CXX:-g++}" -std=c++11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter \
  -I"$test_dir/../../Enclave" \
  -I"${SGX_SDK:-/opt/intel/sgxsdk}/include" \
  "$test_dir/assembly_smoke.cpp" \
  "$test_dir/../../Enclave/SubSample_v2/OFR/OFR.cpp" \
  "$test_dir/../../Enclave/SubSample_v2/OFR/helper.cpp" \
  -o "$test_build/assembly_smoke"
"$test_build/assembly_smoke"
