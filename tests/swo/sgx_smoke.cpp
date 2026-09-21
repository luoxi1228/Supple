// Exercise the generated ECALL bridges, real AES-GCM and enclave algorithms.
// Launch directly to avoid the application's persistent launch-token file.
#include <sgx_urts.h>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include "../../Untrusted/Enclave_u.h"
#include "../../Application/gcm.h"

static void verify(sgx_enclave_id_t eid, size_t n, size_t m, size_t k,
                   size_t threads, unsigned char *input_key,
                   unsigned char *output_key)
{
  const size_t width = 24, encrypted_width = width + 28;
  std::fprintf(stderr, "Running SGX SIM %s n=%zu m=%zu k=%zu threads=%zu\n",
               threads == 0 ? "serial" : "parallel", n, m, k, threads);
  std::vector<unsigned char> input(n * encrypted_width);
  for (size_t i = 0; i < n; ++i)
  {
    unsigned char record[width];
    std::fill(record, record + width, static_cast<unsigned char>(i * 19));
    const uint64_t id = i;
    std::memcpy(record, &id, sizeof(id));
    unsigned char *row = input.data() + i * encrypted_width;
    std::memcpy(row, &id, sizeof(id));
    assert(gcm_encrypt(record, width, nullptr, 0, input_key, row, 12,
                        row + 12, row + 12 + width) == width);
  }
  std::vector<unsigned char> output(m * k * encrypted_width, 0xff);
  enc_ret timing{};
  const sgx_status_t status = threads == 0
      ? DecSuppleSWO(eid, input.data(), n, m, k, encrypted_width,
                     output.data(), &timing)
      : DecSuppleSWO_parallel(eid, input.data(), n, m, k, encrypted_width,
                              output.data(), &timing, threads);
  if (status != SGX_SUCCESS)
    std::fprintf(stderr, "ECALL failed: 0x%x\n", status);
  assert(status == SGX_SUCCESS);
  for (size_t j = 0; j < k; ++j)
  {
    std::vector<uint64_t> ids;
    for (size_t i = 0; i < m; ++i)
    {
      unsigned char record[width];
      unsigned char *row = output.data() + (j * m + i) * encrypted_width;
      assert(gcm_decrypt(row + 12, width, nullptr, 0, row + 12 + width,
                        output_key, row, 12, record) == width);
      uint64_t id = n;
      std::memcpy(&id, record, sizeof(id));
      assert(id < n);
      for (size_t b = sizeof(id); b < width; ++b)
        assert(record[b] == static_cast<unsigned char>(id * 19));
      ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    assert(std::adjacent_find(ids.begin(), ids.end()) == ids.end());
  }
  std::printf("PASS: SGX SIM %s n=%zu m=%zu k=%zu threads=%zu\n",
              threads == 0 ? "serial" : "parallel", n, m, k, threads);
}

int main(int argc, char **argv)
{
  std::setbuf(stdout, nullptr);
  if (argc < 2 || argc > 3 ||
      (argc == 3 && std::strcmp(argv[2], "--parallel") != 0))
  {
    std::fprintf(stderr, "Usage: %s ENCLAVE [--parallel]\n", argv[0]);
    return 2;
  }
  sgx_launch_token_t token{};
  int updated = 0;
  sgx_enclave_id_t eid = 0;
  std::fprintf(stderr, "Creating SGX SIM enclave: %s\n", argv[1]);
  const sgx_status_t status = sgx_create_enclave(argv[1], SGX_DEBUG_FLAG,
      &token, &updated, &eid, nullptr);
  if (status != SGX_SUCCESS)
  {
    std::fprintf(stderr, "sgx_create_enclave failed: 0x%x\n", status);
    return 1;
  }
  unsigned char input_key[16] = {1}, output_key[16] = {2};
  std::fprintf(stderr, "Loading test keys\n");
  assert(Enclave_loadTestKeys(eid, input_key, output_key) == SGX_SUCCESS);
  verify(eid, 9, 3, 3, 0, input_key, output_key);
  verify(eid, 8, 2, 8, 0, input_key, output_key);
  verify(eid, 8, 8, 3, 0, input_key, output_key);
  verify(eid, 9, 2, 1, 0, input_key, output_key);
  verify(eid, 65, 8, 129, 0, input_key, output_key);
  if (argc == 3)
  {
    verify(eid, 9, 3, 3, 1, input_key, output_key);
    verify(eid, 4096, 512, 16, 2, input_key, output_key);
    verify(eid, 4096, 512, 16, 4, input_key, output_key);
  }
  assert(sgx_destroy_enclave(eid) == SGX_SUCCESS);
}
