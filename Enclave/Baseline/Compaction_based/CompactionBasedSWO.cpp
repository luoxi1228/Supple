#include "CompactionBasedSWO.hpp"
#include "../../MemoryProfile.hpp"
#include "../../SubSample/SubSample.hpp"
#ifndef BEFTS_MODE
#include "../../ObliviousPrimitives.hpp"
#include "../../utils.hpp"
#endif
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>

namespace {
void ResetMetrics(enc_ret *ret) {
    ret->ptime = ret->gen_perm_time = ret->apply_perm_time = 0.0;
#ifdef COUNT_OSWAPS
    ret->OSWAP_count = 0;
#endif
}

bool ValidDimensions(size_t n, size_t m, size_t k, size_t width) {
    // TightCompact's prefix counts and markGen's thresholds are uint32_t.
    // The dispatch supports 4/12-byte records and positive multiples of 8.
    return n != 0 && n <= UINT32_MAX && n < SIZE_MAX / sizeof(uint32_t) &&
           m != 0 && m <= n && k != 0 && width != 0 && width <= UINT32_MAX &&
           (width == 4 || width == 12 || width % 8 == 0) &&
           width <= SIZE_MAX / n && k <= SIZE_MAX / m &&
           width <= SIZE_MAX / (k * m);
}

struct FreeBuffer {
    void operator()(unsigned char *buffer) const { free(buffer); }
};
using Buffer = std::unique_ptr<unsigned char, FreeBuffer>;

struct RandomPool {
    RandomPool() { PRB_pool_init(1); }
    ~RandomPool() { PRB_pool_shutdown(); }
    RandomPool(const RandomPool &) = delete;
    RandomPool &operator=(const RandomPool &) = delete;
};
} // namespace

bool CompactionBasedSWO(unsigned char *buffer, size_t N, size_t M, size_t K,
                        size_t block_size, unsigned char *result_buffer,
                        enc_ret *ret) {
    if (ret == nullptr) return false;
    ResetMetrics(ret);
    if (buffer == nullptr || result_buffer == nullptr ||
        !ValidDimensions(N, M, K, block_size)) {
        memory_profile::Fail();
        return false;
    }

    try {
        // One allocation reused by all samples, outside the algorithm timer.
        std::unique_ptr<bool[]> selected(new bool[N]);
#ifdef COUNT_OSWAPS
        const uint64_t initial_oswaps = OSWAP_COUNTER;
#endif
        long begin, marked, end;
        ocall_clock(&begin);
        double marking_ticks = 0.0, applying_ticks = 0.0;
        for (size_t sample = 0; sample < K; ++sample) {
            markGen(N, M, selected.get());
            ocall_clock(&marked);
            TightCompact_v2(buffer, N, block_size, selected.get());
            memcpy(result_buffer + sample * M * block_size, buffer,
                   M * block_size);
            ocall_clock(&end);
            marking_ticks += static_cast<double>(marked - begin);
            applying_ticks += static_cast<double>(end - marked);
            begin = end;
        }
        ret->gen_perm_time = marking_ticks / 1000.0;
        ret->apply_perm_time = applying_ticks / 1000.0;
        ret->ptime = ret->gen_perm_time + ret->apply_perm_time;
#ifdef COUNT_OSWAPS
        ret->OSWAP_count = OSWAP_COUNTER - initial_oswaps;
#endif
        return true;
    } catch (...) {
        ResetMetrics(ret);
        memory_profile::Fail();
        return false;
    }
}

void DecCompactionBasedSWO(unsigned char *encrypted_buffer, size_t N, size_t M,
                           size_t K, size_t encrypted_block_size,
                           unsigned char *encrypted_result_buffer, enc_ret *ret) {
    if (ret == nullptr) return;
    memory_profile::Scope memory(ret);
    ResetMetrics(ret);
    const size_t overhead = SGX_AESGCM_IV_SIZE + SGX_AESGCM_MAC_SIZE;
    if (encrypted_buffer == nullptr || encrypted_result_buffer == nullptr ||
        encrypted_block_size <= overhead ||
        !ValidDimensions(N, M, K, encrypted_block_size - overhead) ||
        encrypted_block_size > SIZE_MAX / N ||
        encrypted_block_size > SIZE_MAX / (K * M)) {
        memory_profile::Fail();
        return;
    }

    try {
        unsigned char *plain = nullptr;
        const size_t width = decryptBuffer(encrypted_buffer, N,
                                           encrypted_block_size, &plain);
        Buffer input(plain);
        if (!input || width == SIZE_MAX) return;
        Buffer output(static_cast<unsigned char *>(malloc(K * M * width)));
        if (!output) {
            memory_profile::Fail();
            return;
        }
        RandomPool random_pool;
        if (!CompactionBasedSWO(input.get(), N, M, K, width, output.get(), ret))
            return;
        if (encryptBuffer(output.get(), K * M, width,
                          encrypted_result_buffer) == SIZE_MAX) return;
        memory.Complete();
    } catch (...) {
        ResetMetrics(ret);
        memory_profile::Fail();
    }
}
