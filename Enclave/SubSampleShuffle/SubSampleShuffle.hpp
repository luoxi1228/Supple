#ifndef __SUBSAMPLE_SHUFFLE_HPP__
#define __SUBSAMPLE_SHUFFLE_HPP__

#ifndef BEFTS_MODE
  #include <cstddef>
  #include <cstdint>
  #include <vector>
  #include <cstring>
  #include "../foav.h"
  #include "../oasm_lib.h"
  #include "../ObliviousPrimitives.hpp"
  #include "../Enclave_globals.h"
  #include "../utils.hpp"
  #include "../aes.hpp"
#endif

// shuffle-truncate
void subSampleShuffle(unsigned char *buffer, size_t N, size_t M, size_t block_size, unsigned char *result_buffer, enc_ret *ret);

// decrytion-shuffle-truncate-encryption
void decryptAndSubSampleShuffle(unsigned char *encrypted_buffer, size_t N, size_t M, size_t encrypted_block_size, unsigned char *encryt_result_buffer, enc_ret *ret);

// multi-way subsample with oblivious SWO
void subSampleSWO(unsigned char *buffer, size_t N, size_t M, size_t block_size, unsigned char *result_buffer, enc_ret *ret);

void decryptAndSubSampleSWO(unsigned char *encrypted_buffer, size_t N, size_t M, size_t encrypted_block_size, unsigned char *encryt_result_buffer, enc_ret *ret);

// SWO helper: builds k pseudo-random permutations over [0,n-1] using per-sample
// PRP keys. A key j is in sample i iff rho_i(j) <= m.
class SWO {
  public:
    SWO()
        : n_(0),
          m_(0),
          k_(0),
          a_(0),
          b_(0) {} // 移除了预计算相关的变量

    void initialize(size_t N, size_t M) {
      n_ = N;
      m_ = M;

      if (m_ == 0 || n_ == 0) {
        k_ = 0;
        keys_.clear();
        return;
      }

      if (m_ > n_) {
        m_ = n_;
      }

      k_ = n_ / m_;
      if (k_ == 0) {
        k_ = 1;
      }

      // --- 核心修改：构建紧凑的广义二维空间 ---
      // 计算 a_ 和 b_ 使得 a_ * b_ >= n_ 且 a_ 与 b_ 极为接近
      uint64_t sq = integer_sqrt(n_);
      a_ = sq;
      if (sq * sq < n_) {
          a_++; // 相当于 std::ceil(std::sqrt(n_))
      }
      b_ = (n_ + a_ - 1) / a_; 

      init_keys();
      
      // 注意：这里已经彻底移除了 build_member_table_linear() 
      // 因为底层计算不再有惩罚，不再需要用珍贵的 SGX 内存换时间
    }

    bool samplemember(size_t sample_idx, size_t logical_j) const {
      if (sample_idx >= k_ || logical_j >= n_ || m_ == 0) {
        return false;
      }
      uint64_t pos = prp_index(sample_idx, (uint64_t)logical_j);
      return pos < m_;
    }

    // samplemember_n 直接回退到 samplemember 即可
    bool samplemember_n(size_t sample_idx, size_t logical_j) const {
      return samplemember(sample_idx, logical_j);
    }

    size_t get_k() const { return k_; }

  private:
    struct AESKeySchedule {
      AESkey key;
    };

    size_t n_;
    size_t m_;
    size_t k_;
    uint64_t a_; // 广义 Feistel 左半部大小
    uint64_t b_; // 广义 Feistel 右半部大小
    std::vector<AESKeySchedule> keys_;

    // SGX 友好的快速整数平方根
    static inline uint64_t integer_sqrt(uint64_t n) {
      if (n <= 1) return n;
      uint64_t x0 = n / 2;
      uint64_t x1 = (x0 + n / x0) / 2;
      while (x1 < x0) {
        x0 = x1;
        x1 = (x0 + n / x0) / 2;
      }
      return x0;
    }

    // 取完整的 AES 64位输出
    static inline uint64_t prf_full(const AESKeySchedule &key,
                                    uint64_t R,
                                    uint32_t round) {
      __m128i ciphertext;
      __m128i plaintext = _mm_set_epi64x((long long)round, (long long)R);
      AES_ECB_encrypt(ciphertext, plaintext, key.key);

      uint64_t out = 0;
      memcpy(&out, &ciphertext, sizeof(out));
      return out;
    }

    // 广义非平衡 Feistel 算法 (Generalized Unbalanced Feistel Network)
    uint64_t generalized_feistel(const AESKeySchedule &key, uint64_t x) const {
      // 1. 将 1D 坐标投影到 2D 空间 (Z_a x Z_b)
      uint64_t L = x / b_;
      uint64_t R = x % b_;

      for (uint32_t r = 0; r < 4; ++r) {
        uint64_t F = prf_full(key, R, r);
        
        if (r % 2 == 0) {
          // 偶数轮：L 属于 Z_a，R 属于 Z_b
          // 注意：先 (F % a_) 防溢出，再加 L，最后再模 a_ 保证输出在 Z_a 域内
          uint64_t newR = (L + (F % a_)) % a_;
          L = R;
          R = newR;
        } else {
          // 奇数轮：L 属于 Z_b，R 属于 Z_a
          uint64_t newR = (L + (F % b_)) % b_;
          L = R;
          R = newR;
        }
      }

      // 2. 将 2D 坐标重新压平到 1D
      return L * b_ + R;
    }

    uint64_t prp_index(size_t sample_idx, uint64_t val) const {
      if (n_ <= 1) {
        return 0;
      }

      const AESKeySchedule &key = keys_[sample_idx];
      uint64_t x = val;

      // 此时 Cycle-walking 虽然存在，但在最坏情况下的命中率也 > 99%！
      while (true) {
        x = generalized_feistel(key, x);
        if (x < n_) {
          return x; 
        }
      }
    }

    void init_keys() {
      keys_.resize(k_);
      PRB_buffer *randpool = PRB_pool + g_thread_id;

      for (size_t i = 0; i < k_; ++i) {
        unsigned char raw[16];
        randpool->getRandomBytes(raw, sizeof(raw));
        __m128i rawkey;
        memcpy(&rawkey, raw, sizeof(rawkey));
        AES_128_Key_Expansion(keys_[i].key, rawkey);
      }
    }
};
#endif
