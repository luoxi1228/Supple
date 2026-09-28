#!/usr/bin/env bash
set -euo pipefail
test_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
test_build=$(mktemp -d /tmp/supple-ofr-bench.XXXXXX)
"${CXX:-g++}" -std=c++11 -O3 -DNDEBUG -Wno-unused-parameter \
  -I"$test_dir/../../Enclave" \
  -I"${SGX_SDK:-/opt/intel/sgxsdk}/include" \
  "$test_dir/phase_benchmark.cpp" \
  "$test_dir/../../Enclave/SubSample_v2/OFR/OFR.cpp" \
  "$test_dir/../../Enclave/SubSample_v2/OFR/helper.cpp" \
  -o "$test_build/phase_benchmark"
"$test_build/phase_benchmark" "$@"
