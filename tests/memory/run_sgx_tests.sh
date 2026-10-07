#!/usr/bin/env bash
set -euo pipefail
test_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
project_dir=$(cd -- "$test_dir/../.." && pwd)
test_build=$(mktemp -d /tmp/supple-memory-sgx.XXXXXX)
sdk_dir=${SGX_SDK:-/opt/intel/sgxsdk}
cd "$project_dir"
echo "SGX test artifacts: $test_build" >&2
make -f tests/ffos_c/sgx_smoke.mk -B -j4 enclave.so >&2
"$sdk_dir/bin/x64/sgx_sign" sign -key Enclave/Enclave_private.pem \
  -enclave enclave.so -out "$test_build/enclave.signed.so" \
  -config tests/ffos_c/sgx_smoke.config.xml >&2
"$sdk_dir/bin/x64/sgx_edger8r" --untrusted Enclave/Enclave.edl \
  --search-path Enclave --search-path "$sdk_dir/include" --untrusted-dir Untrusted
gcc -fPIC -O3 -I"$sdk_dir/include" -IUntrusted \
  -c Untrusted/Enclave_u.c -o "$test_build/bridge.o"
for source in Untrusted/Untrusted.cpp Untrusted/Edger8rSyntax/*.cpp Untrusted/TrustedLibrary/*.cpp; do
  object=${source//\//_}
  g++ -std=c++11 -fPIC -O3 -I"$sdk_dir/include" -IUntrusted \
    -c "$source" -o "$test_build/$object.o"
done
g++ -shared "$test_build"/*.o -L"$sdk_dir/lib64" \
  -lsgx_urts_sim -lsgx_uae_service_sim -pthread \
  -Wl,-rpath,"$sdk_dir/lib64" -o "$test_build/libOSort.so"
g++ -std=c++11 -O3 -I"$sdk_dir/include" tests/memory/sgx_test.cpp Application/gcm.cpp \
  -L"$test_build" -lOSort -L"$sdk_dir/lib64" -lsgx_urts_sim -lcrypto -pthread \
  -Wl,-rpath,"$test_build" -Wl,-rpath,"$sdk_dir/lib64" -o "$test_build/sgx_test"
g++ -std=c++11 -O0 -I"$sdk_dir/include" Application/Application.cpp Application/gcm.cpp \
  -L"$test_build" -lOSort -L"$sdk_dir/lib64" -lsgx_urts_sim -lcrypto -pthread \
  -Wl,-rpath,"$test_build" -Wl,-rpath,"$sdk_dir/lib64" -o "$test_build/application"
"$test_build/sgx_test" "$test_build/enclave.signed.so" "${@}"
if [ "$#" -eq 0 ]; then
  PYTHONDONTWRITEBYTECODE=1 python3 tests/memory/test_sgx_results.py "$test_build"
fi
