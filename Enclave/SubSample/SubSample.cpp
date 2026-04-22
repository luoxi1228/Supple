#include "SubSample.hpp"
#include "../ObliviousPrimitives.hpp"
#include "../RecursiveShuffle/RecursiveShuffle.hpp"
#include <algorithm>
#include <memory>
#include <cstring>
#include <cstdlib>
#include <new>
#include <cmath>

void subSample(unsigned char *buffer,
               size_t N,
               size_t M,
               size_t block_size,
               unsigned char *result_buffer,
               enc_ret *ret)
{
  if (M > N)
  {
    M = N;
  }
  if (M == 0 || result_buffer == NULL)
  {
    ret->ptime = 0.0;
#ifdef COUNT_OSWAPS
    ret->OSWAP_count = 0;
#endif
    return;
  }

  bool *mark_list = new (std::nothrow) bool[N]{};
  if (!mark_list)
  {
    printf("Allocating memory failed in subSample\n");
    ret->ptime = 0.0;
#ifdef COUNT_OSWAPS
    ret->OSWAP_count = 0;
#endif
    return;
  }

  long t0, t1;

  markGen(N, M, mark_list);

  ocall_clock(&t0);
  OP_TightCompact_v2(buffer, N, block_size, mark_list);
  RecursiveShuffle_M2(buffer, M, block_size); 
  ocall_clock(&t1);

  encryptBuffer(buffer, (uint64_t)M, block_size, result_buffer);


  ret->ptime = ((double)(t1 - t0)) / 1000.0;

#ifdef COUNT_OSWAPS
  ret->OSWAP_count = OSWAP_COUNTER;
#endif

  delete[] mark_list;
}

void subSampleMulti(unsigned char *buffer,
                    size_t N,
                    size_t M,
                    size_t K,
                    size_t block_size,
                    unsigned char *result_buffer,
                    enc_ret *ret)
{
  if (M > N)
  {
    M = N;
  }
  if (M == 0 || K == 0 || result_buffer == NULL)
  {
    ret->ptime = 0.0;
    ret->gen_perm_time = 0.0;
    ret->apply_perm_time = 0.0;
#ifdef COUNT_OSWAPS
    ret->OSWAP_count = 0;
#endif
    return;
  }

  long t0, t1;
  ocall_clock(&t0);

  const size_t mark_words = (K + wordBits - 1) / wordBits;
  size_t mark_list_size = 0;
  if (MulOverflowSizeT(N, mark_words, &mark_list_size))
  {
    printf("subSampleMulti: overflow computing mark_list_size\n");
    ret->ptime = 0.0;
    ret->gen_perm_time = 0.0;
    ret->apply_perm_time = 0.0;
#ifdef COUNT_OSWAPS
    ret->OSWAP_count = 0;
#endif
    return;
  }

  std::vector<size_t> mark_list(mark_list_size, 0);
  markGenMulti(N, M, K, mark_list.data());

  ocall_clock(&t1);
  ret->gen_perm_time = ((double)(t1 - t0)) / 1000.0;

  size_t total_blocks = 0;
  if (MulOverflowSizeT(M, K, &total_blocks))
  {
    printf("subSampleMulti: overflow computing total_blocks (M*K)\n");
    ret->apply_perm_time = 0.0;
    ret->ptime = ret->gen_perm_time;
#ifdef COUNT_OSWAPS
    ret->OSWAP_count = 0;
#endif
    return;
  }

  size_t plain_result_bytes = 0;
  if (MulOverflowSizeT(total_blocks, block_size, &plain_result_bytes))
  {
    printf("subSampleMulti: overflow computing plain_result_bytes\n");
    ret->apply_perm_time = 0.0;
    ret->ptime = ret->gen_perm_time;
#ifdef COUNT_OSWAPS
    ret->OSWAP_count = 0;
#endif
    return;
  }

  std::vector<unsigned char> plain_result(plain_result_bytes, 0);

  PairWorkspace left_ws;
  PairWorkspace right_ws;

  ocall_clock(&t0);

  const size_t written_blocks = RecSampling(buffer, mark_list.data(),
                                            N, mark_words,
                                            K, M, block_size,
                                            plain_result.data(),
                                            total_blocks,
                                            &left_ws, &right_ws,
                                            nullptr, &mark_list);
  (void)written_blocks;

  ocall_clock(&t1);
  ret->apply_perm_time = ((double)(t1 - t0)) / 1000.0;
  ret->ptime = ret->gen_perm_time + ret->apply_perm_time;

  encryptBuffer(plain_result.data(), (uint64_t)total_blocks, block_size, result_buffer);

#ifdef COUNT_OSWAPS
  ret->OSWAP_count = OSWAP_COUNTER;
#endif
}

void subSampleMulti_opt(unsigned char *buffer, size_t N, size_t M, size_t K,
                        size_t block_size, unsigned char *result_buffer, enc_ret *ret) {
    if (M > N) M = N;
    if (M == 0 || K == 0 || result_buffer == NULL) {
        ret->ptime = 0.0; ret->gen_perm_time = 0.0; ret->apply_perm_time = 0.0;
#ifdef COUNT_OSWAPS
        ret->OSWAP_count = 0;
#endif
        return;
    }

    long t0, t1;
    ocall_clock(&t0);

    const size_t mark_words = (K + wordBits - 1) / wordBits;
    std::vector<size_t> mark_list(N * mark_words, 0);
    markGenMulti(N, M, K, mark_list.data());

    // --- 分配极致压缩的布尔数组 ---
    // 先计算总 bit 数，然后向上取整算出所需 byte 数
    size_t total_bits = nodeNum(N, M, K);
    size_t total_bytes = (total_bits + 7) / 8;
    
    // 务必使用 () 初始化为 0，因为按位压缩时用到 |= 操作
    uint8_t *B = new (std::nothrow) uint8_t[total_bytes](); 
    if (!B) {
#ifdef COUNT_OSWAPS
        ret->OSWAP_count = 0;
#endif
        return;
    }

    size_t bit_offset = 0;
    MarkWorkspace_opt left_mws, right_mws;
    RecPrecompute_opt(mark_list.data(), N, mark_words, K, M, B, bit_offset, &left_mws, &right_mws);
    
    // SGX 内存解压：立刻释放庞大的 Mark 表及其临时工作区
    std::vector<size_t>().swap(mark_list); 
    std::vector<size_t>().swap(left_mws.mark_buf);
    std::vector<size_t>().swap(right_mws.mark_buf);

    ocall_clock(&t1);
    ret->gen_perm_time = ((double)(t1 - t0)) / 1000.0;

    size_t total_blocks = M * K;
    std::vector<unsigned char> plain_result(total_blocks * block_size, 0);

    bit_offset = 0; // 重置游标，准备精确复播
    DataWorkspace_opt left_dws, right_dws;
    
#ifdef COUNT_OSWAPS
    // 记录在线阶段开始前的 OSwap 数量基准值
    uint64_t initial_oswaps = OSWAP_COUNTER;
#endif

    // --- 在线阶段 ---
    ocall_clock(&t0);

    RecSampling_opt(buffer, N, K, M, block_size, plain_result.data(), total_blocks,
                    B, bit_offset, &left_dws, &right_dws);

    ocall_clock(&t1);
    ret->apply_perm_time = ((double)(t1 - t0)) / 1000.0;
    ret->ptime = ret->gen_perm_time + ret->apply_perm_time;

#ifdef COUNT_OSWAPS
    // 仅计算在线阶段引发的 OSwap 差值
    ret->OSWAP_count = OSWAP_COUNTER - initial_oswaps;
#endif

    encryptBuffer(plain_result.data(), (uint64_t)total_blocks, block_size, result_buffer);
    
    delete[] B;
}

void decryptAndSubSample(unsigned char *encrypted_buffer,
                         size_t N,
                         size_t M,
                         size_t encrypted_block_size,
                         unsigned char *encryt_result_buffer,
                         enc_ret *ret)
{
  unsigned char *decrypted_buffer = NULL;
  size_t decrypted_block_size =
      decryptBuffer(encrypted_buffer, (uint64_t)N, encrypted_block_size, &decrypted_buffer);

  if (M > N)
  {
    M = N;
  }
  if (M == 0)
  {
    free(decrypted_buffer);
    ret->ptime = 0.0;
#ifdef COUNT_OSWAPS
    ret->OSWAP_count = 0;
#endif
    return;
  }

  PRB_pool_init(1);
  subSample(decrypted_buffer, N, M, decrypted_block_size, encryt_result_buffer, ret);
  PRB_pool_shutdown();

  free(decrypted_buffer);
}

void decryptAndSubSampleMulti(unsigned char *encrypted_buffer,
                              size_t N,
                              size_t M,
                              size_t K,
                              size_t encrypted_block_size,
                              unsigned char *encryt_result_buffer,
                              enc_ret *ret)
{
  unsigned char *decrypted_buffer = NULL;
  size_t decrypted_block_size =
      decryptBuffer(encrypted_buffer, (uint64_t)N, encrypted_block_size, &decrypted_buffer);

  if (decrypted_buffer == NULL || decrypted_block_size == static_cast<size_t>(-1))
  {
    ret->ptime = 0.0;
    ret->gen_perm_time = 0.0;
    ret->apply_perm_time = 0.0;
#ifdef COUNT_OSWAPS
    ret->OSWAP_count = 0;
#endif
    return;
  }

  if (M > N)
  {
    M = N;
  }
  if (M == 0 || K == 0)
  {
    free(decrypted_buffer);
    ret->ptime = 0.0;
    ret->gen_perm_time = 0.0;
    ret->apply_perm_time = 0.0;
#ifdef COUNT_OSWAPS
    ret->OSWAP_count = 0;
#endif
    return;
  }

  PRB_pool_init(1);
  subSampleMulti(decrypted_buffer, N, M, K, decrypted_block_size, encryt_result_buffer, ret);
  // subSampleMulti_opt(decrypted_buffer, N, M, K, decrypted_block_size, encryt_result_buffer, ret);
  PRB_pool_shutdown();

  free(decrypted_buffer);
}


void decryptAndSubSampleMulti_opt(unsigned char *encrypted_buffer,
                              size_t N,
                              size_t M,
                              size_t K,
                              size_t encrypted_block_size,
                              unsigned char *encryt_result_buffer,
                              enc_ret *ret)
{
  unsigned char *decrypted_buffer = NULL;
  size_t decrypted_block_size =
      decryptBuffer(encrypted_buffer, (uint64_t)N, encrypted_block_size, &decrypted_buffer);

  if (decrypted_buffer == NULL || decrypted_block_size == static_cast<size_t>(-1))
  {
    ret->ptime = 0.0;
    ret->gen_perm_time = 0.0;
    ret->apply_perm_time = 0.0;
#ifdef COUNT_OSWAPS
    ret->OSWAP_count = 0;
#endif
    return;
  }

  if (M > N)
  {
    M = N;
  }
  if (M == 0 || K == 0)
  {
    free(decrypted_buffer);
    ret->ptime = 0.0;
    ret->gen_perm_time = 0.0;
    ret->apply_perm_time = 0.0;
#ifdef COUNT_OSWAPS
    ret->OSWAP_count = 0;
#endif
    return;
  }

  PRB_pool_init(1);
  // subSampleMulti(decrypted_buffer, N, M, K, decrypted_block_size, encryt_result_buffer, ret);
  subSampleMulti_opt(decrypted_buffer, N, M, K, decrypted_block_size, encryt_result_buffer, ret);
  PRB_pool_shutdown();

  free(decrypted_buffer);
}




void markGen(size_t n, size_t m, bool *M)
{
  if (m > n)
  {
    m = n;
  }

  uint64_t left_to_mark = m;
  uint64_t total_left = n;
  PRB_buffer *randpool = PRB_pool + g_thread_id;
  static const size_t kMaxCoins = 2048;
  uint32_t coins[kMaxCoins];
  size_t coinsleft = 0;

  FOAV_SAFE_CNTXT(SubSample_marking_m, n)
  for (size_t i = 0; i < n; i++)
  {
    FOAV_SAFE2_CNTXT(SubSample_marking_m, i, coinsleft)
    if (coinsleft == 0)
    {
      size_t numcoins = (n - i);
      FOAV_SAFE_CNTXT(SubSample_marking_m, numcoins)
      if (numcoins > kMaxCoins)
      {
        numcoins = kMaxCoins;
      }
      randpool->getRandomBytes((unsigned char *)coins,
                               sizeof(coins[0]) * numcoins);
      coinsleft = numcoins;
    }

    uint32_t random_coin;
    random_coin = (total_left * coins[--coinsleft]) >> 32;
    uint32_t mark_threshold = total_left - left_to_mark;
    uint8_t mark_element = oge_set_flag(random_coin, mark_threshold);

    FOAV_SAFE_CNTXT(SubSample_marking_m, i)
    M[i] = mark_element;
    left_to_mark -= mark_element;
    total_left--;
    FOAV_SAFE2_CNTXT(SubSample_marking_m, i, n)
  }
}

void markGenMulti(size_t n, size_t m, size_t k, size_t *M)
{
  size_t mark_words = (k + wordBits - 1) / wordBits;
  if (M == NULL || n == 0 || k == 0 || mark_words == 0)
  {
    return;
  }

  if (m > n)
  {
    m = n;
  }

  std::vector<uint64_t> left_to_mark(k, m);
  std::vector<uint64_t> total_left(k, n);

  PRB_buffer *randpool = PRB_pool + g_thread_id;
  static const size_t kMaxCoins = 2048;
  uint32_t coins[kMaxCoins];
  size_t coinsleft = 0;

  for (size_t i = 0; i < n; i++)
  {
    size_t *current_mark_ptr = &M[i * mark_words];

    for (size_t j = 0; j < k; j++)
    {
      // 补充随机数池（这里依赖于公开变量 coinsleft，不破坏不经意性）
      if (coinsleft == 0)
      {
        randpool->getRandomBytes(reinterpret_cast<unsigned char *>(coins),
                                 sizeof(coins[0]) * kMaxCoins);
        coinsleft = kMaxCoins;
      }

      uint32_t random_coin = coins[--coinsleft];
      
      // 计算阈值并生成布尔标记 (比较操作编译为 cmp + setb，属于恒定时间指令)
      uint64_t threshold = (static_cast<uint64_t>(left_to_mark[j]) << 32) / total_left[j];
      uint8_t mark_element = static_cast<uint8_t>(random_coin < threshold);

      size_t word_index = j / wordBits;
      size_t bit_index = j % wordBits;

      // ------------------------------------------------------------------
      // Fully Oblivious 核心修复区：消除 if (mark_element) 的条件分支
      // ------------------------------------------------------------------
      
      // 1. 无分支掩码写入：
      // 当 mark_element 为 1 时，生成目标位置的掩码并执行位或 (OR)
      // 当 mark_element 为 0 时，位移结果为 0，位或操作不会改变原值
      current_mark_ptr[word_index] |= (static_cast<size_t>(mark_element) << bit_index);

      // 2. 无分支算术更新：
      // 直接减去 mark_element，无论它是 1 还是 0，都不会引起指令跳转
      left_to_mark[j] -= mark_element;

      // 无论当前元素是否被选中，剩余总数都会固定递减
      total_left[j]--;
    }
  }
}

void ORCompactPair(const unsigned char *data,
                   const size_t *mark,
                   size_t len_items,
                   size_t src_mark_words,
                   const bool *selected,
                   size_t target_size,
                   size_t block_size,
                   size_t slice_start,
                   size_t slice_bits,
                   PairWorkspace &ws)
{
  if (block_size == 0)
  {
    ws.item_count = 0;
    ws.mark_words_per_item = 0;
    return;
  }

  const size_t dst_mark_words = (slice_bits + wordBits - 1) / wordBits;
  const size_t usable_len = len_items;

  ws.ensure_capacity(usable_len, block_size, dst_mark_words);

  if (usable_len == 0 || target_size == 0)
  {
    ws.item_count = target_size;
    ws.mark_words_per_item = dst_mark_words;
    return;
  }

  // 1) 拷整段 data 到工作区
  std::memcpy(ws.data_ptr(), data, usable_len * block_size);

  // 2) 生成 sliced mark 到工作区
  if (dst_mark_words > 0)
  {
    std::fill(ws.mark_buf.begin(),
              ws.mark_buf.begin() + usable_len * dst_mark_words,
              size_t(0));

    for (size_t i = 0; i < usable_len; i++)
    {
      const size_t *src_row = &mark[i * src_mark_words];
      size_t *dst_row = ws.mark_ptr() + (i * dst_mark_words);
      ProjectMarkSlice(src_row,
                       src_mark_words,
                       slice_start,
                       slice_bits,
                       dst_row,
                       dst_mark_words);
    }
  }

  // 3) 固定长度 compact
  OP_TightCompact_v2(ws.data_ptr(),
                     usable_len,
                     block_size,
                     const_cast<bool *>(selected));

  if (dst_mark_words > 0)
  {
    OP_TightCompact_v2(reinterpret_cast<unsigned char *>(ws.mark_ptr()),
                       usable_len,
                       sizeof(size_t) * dst_mark_words,
                       const_cast<bool *>(selected));
  }

  // 4) 逻辑上只保留 target_size
  ws.item_count = target_size;
  ws.mark_words_per_item = dst_mark_words;
}

size_t RecSampling(const unsigned char *data,
                   const size_t *mark,
                   size_t len_items,
                   size_t mark_words,
                   size_t k,
                   size_t M,
                   size_t block_size,
                   unsigned char *out,
                   size_t out_capacity,
                   PairWorkspace *left_ws,
                   PairWorkspace *right_ws,
                   std::vector<unsigned char> *data_owner,
                   std::vector<size_t> *mark_owner)
{
  if (block_size == 0 || out_capacity == 0)
  {
    return 0;
  }

  if (k == 1 || len_items == 0 || mark_words == 0)
  {
    size_t copy_count = std::min(M, len_items);
    if (copy_count > out_capacity)
    {
      copy_count = out_capacity;
    }

    if (copy_count > 0)
    {
      std::memcpy(out, data, copy_count * block_size);
      RecursiveShuffle_M2(out, copy_count, block_size);
    }

    if (data_owner != nullptr)
    {
      std::vector<unsigned char>().swap(*data_owner);
    }
    if (mark_owner != nullptr)
    {
      std::vector<size_t>().swap(*mark_owner);
    }
    return copy_count;
  }

  const size_t k_left = k / 2;
  const size_t k_right = k - k_left;

  const size_t size_L = std::min(len_items, k_left * M);
  const size_t size_R = std::min(len_items, k_right * M);

  bool *selected_L = AcquireSelectedScratchA(len_items);
  bool *selected_R = AcquireSelectedScratchB(len_items);
  if (selected_L == nullptr || selected_R == nullptr)
  {
    return 0;
  }

  size_t *mark_rangeL = AcquireMarkScratchA(mark_words);
  size_t *mark_rangeR = AcquireMarkScratchB(mark_words);
  if (mark_rangeL == nullptr || mark_rangeR == nullptr)
  {
    return 0;
  }

  for (size_t w = 0; w < mark_words; w++)
  {
    mark_rangeL[w] = MakeRangeMarkWord(0, k_left, w);
    mark_rangeR[w] = MakeRangeMarkWord(k_left, k, w);
  }

  for (size_t i = 0; i < len_items; i++)
  {
    const size_t *mark_row = &mark[i * mark_words];
    uint8_t in_L = 0;
    uint8_t in_R = 0;

    for (size_t w = 0; w < mark_words; w++)
    {
      in_L |= (mark_row[w] & mark_rangeL[w]) != 0;
      in_R |= (mark_row[w] & mark_rangeR[w]) != 0;
    }

    selected_L[i] = (in_L != 0);
    selected_R[i] = (in_R != 0);
  }

  PairWorkspace local_left_ws;
  PairWorkspace local_right_ws;
  PairWorkspace *LWS = (left_ws != nullptr) ? left_ws : &local_left_ws;
  PairWorkspace *RWS = (right_ws != nullptr) ? right_ws : &local_right_ws;

  size_t written_L;
  {
    ORCompactPair(data, mark, len_items, mark_words,
                  selected_L, size_L, block_size,
                  0, k_left, *LWS);

    const size_t left_words = (k_left + wordBits - 1) / wordBits;

    written_L = RecSampling(LWS->data_ptr(),
                            LWS->mark_ptr(),
                            LWS->item_count,
                            left_words,
                            k_left, M, block_size,
                            out, out_capacity,
                            LWS, RWS,
                            nullptr, nullptr);
  }

  size_t written_R;
  {
    ORCompactPair(data, mark, len_items, mark_words,
                  selected_R, size_R, block_size,
                  k_left, k_right, *RWS);

    if (data_owner != nullptr)
    {
      std::vector<unsigned char>().swap(*data_owner);
    }
    if (mark_owner != nullptr)
    {
      std::vector<size_t>().swap(*mark_owner);
    }

    const size_t right_words = (k_right + wordBits - 1) / wordBits;

    written_R = RecSampling(RWS->data_ptr(),
                            RWS->mark_ptr(),
                            RWS->item_count,
                            right_words,
                            k_right, M, block_size,
                            out + (written_L * block_size),
                            out_capacity - written_L,
                            LWS, RWS,
                            nullptr, nullptr);
  }

  return written_L + written_R;
}


void CompactMark_opt(const size_t *mark, size_t len_items, size_t src_mark_words,
                     const bool *selected, size_t target_size, size_t slice_start,
                     size_t slice_bits, MarkWorkspace_opt &ws) {
    const size_t dst_mark_words = (slice_bits + wordBits - 1) / wordBits;
    ws.ensure_capacity(len_items, dst_mark_words);

    if (len_items == 0 || target_size == 0 || dst_mark_words == 0) {
        ws.item_count = target_size;
        return;
    }

    std::fill(ws.mark_buf.begin(), ws.mark_buf.begin() + len_items * dst_mark_words, 0);
    for (size_t i = 0; i < len_items; i++) {
        ProjectMarkSlice(&mark[i * src_mark_words], src_mark_words, slice_start, slice_bits,
                         ws.mark_ptr() + (i * dst_mark_words), dst_mark_words);
    }
    
    OP_TightCompact_v2(reinterpret_cast<unsigned char *>(ws.mark_ptr()),
                       len_items, sizeof(size_t) * dst_mark_words, const_cast<bool *>(selected));
    
    ws.item_count = target_size;
}

void RecPrecompute_opt(const size_t *mark, size_t len_items, size_t mark_words,
                       size_t k, size_t M, uint8_t *B, size_t &bit_offset,
                       MarkWorkspace_opt *left_ws, MarkWorkspace_opt *right_ws ) {
    if (k == 1 || len_items == 0 || mark_words == 0) return;

    size_t k_left = k / 2;
    size_t k_right = k - k_left;

    // 获取临时解包缓冲区
    bool *selected_L = AcquireSelectedScratchA(len_items);
    bool *selected_R = AcquireSelectedScratchB(len_items);
    size_t *mark_rangeL = AcquireMarkScratchA(mark_words);
    size_t *mark_rangeR = AcquireMarkScratchB(mark_words);

    for (size_t w = 0; w < mark_words; w++) {
        mark_rangeL[w] = MakeRangeMarkWord(0, k_left, w);
        mark_rangeR[w] = MakeRangeMarkWord(k_left, k, w);
    }

    // 1. 计算掩码并存入临时 bool 数组
    for (size_t i = 0; i < len_items; i++) {
        const size_t *mark_row = &mark[i * mark_words];
        uint8_t in_L = 0, in_R = 0;
        for (size_t w = 0; w < mark_words; w++) {
            in_L |= (mark_row[w] & mark_rangeL[w]) != 0;
            in_R |= (mark_row[w] & mark_rangeR[w]) != 0;
        }
        selected_L[i] = (in_L != 0);
        selected_R[i] = (in_R != 0);
    }

    // 2. 极限压缩：按位并入全局数组 B
    for (size_t i = 0; i < len_items; i++) {
        if (selected_L[i]) {
            size_t pos = bit_offset + i;
            B[pos / 8] |= (1 << (pos % 8));
        }
    }
    bit_offset += len_items;

    for (size_t i = 0; i < len_items; i++) {
        if (selected_R[i]) {
            size_t pos = bit_offset + i;
            B[pos / 8] |= (1 << (pos % 8));
        }
    }
    bit_offset += len_items;

    // 3. 执行 Compact 及递归
    size_t size_L = std::min(len_items, M * k_left);
    size_t size_R = std::min(len_items, M * k_right);

    MarkWorkspace_opt local_left_ws, local_right_ws;
    MarkWorkspace_opt *LWS = (left_ws != nullptr) ? left_ws : &local_left_ws;
    MarkWorkspace_opt *RWS = (right_ws != nullptr) ? right_ws : &local_right_ws;

    CompactMark_opt(mark, len_items, mark_words, selected_L, size_L, 0, k_left, *LWS);
    CompactMark_opt(mark, len_items, mark_words, selected_R, size_R, k_left, k_right, *RWS);

    const size_t left_words = (k_left + wordBits - 1) / wordBits;
    const size_t right_words = (k_right + wordBits - 1) / wordBits;

    RecPrecompute_opt(LWS->mark_ptr(), LWS->item_count, left_words, k_left, M, B, bit_offset, LWS, RWS);
    RecPrecompute_opt(RWS->mark_ptr(), RWS->item_count, right_words, k_right, M, B, bit_offset, LWS, RWS);
}

void CompactData_opt(const unsigned char *data, size_t len_items, const bool *selected,
                     size_t target_size, size_t block_size, DataWorkspace_opt &ws) {
    if (block_size == 0) return;
    ws.ensure_capacity(len_items, block_size);

    if (len_items == 0 || target_size == 0) {
        ws.item_count = target_size;
        return;
    }

    std::memcpy(ws.data_ptr(), data, len_items * block_size);
    OP_TightCompact_v2(ws.data_ptr(), len_items, block_size, const_cast<bool *>(selected));
    ws.item_count = target_size;
}

size_t RecSampling_opt(const unsigned char *data, size_t len_items, size_t k, size_t M,
                       size_t block_size, unsigned char *out, size_t out_capacity,
                       const uint8_t *B, size_t &bit_offset,
                       DataWorkspace_opt *left_ws, DataWorkspace_opt *right_ws) {
    if (block_size == 0 || out_capacity == 0) return 0;

    if (k == 1 || len_items == 0) {
        size_t copy_count = std::min(M, len_items);
        if (copy_count > out_capacity) copy_count = out_capacity;

        if (copy_count > 0) {
            std::memcpy(out, data, copy_count * block_size);
            RecursiveShuffle_M2(out, copy_count, block_size);
        }
        return copy_count;
    }

    size_t k_left = k / 2;
    size_t k_right = k - k_left;

    // 获取临时解包缓冲区
    bool *selected_L = AcquireSelectedScratchA(len_items);
    bool *selected_R = AcquireSelectedScratchB(len_items);

    // 1. 流式解包：从压缩位数组 B 还原到 bool 数组
    for (size_t i = 0; i < len_items; i++) {
        size_t pos = bit_offset + i;
        selected_L[i] = (B[pos / 8] & (1 << (pos % 8))) != 0;
    }
    bit_offset += len_items;

    for (size_t i = 0; i < len_items; i++) {
        size_t pos = bit_offset + i;
        selected_R[i] = (B[pos / 8] & (1 << (pos % 8))) != 0;
    }
    bit_offset += len_items;

    // 2. 执行数据 Compact 与递归
    size_t size_L = std::min(len_items, k_left * M);
    size_t size_R = std::min(len_items, k_right * M);

    DataWorkspace_opt local_left_ws, local_right_ws;
    DataWorkspace_opt *LWS = (left_ws != nullptr) ? left_ws : &local_left_ws;
    DataWorkspace_opt *RWS = (right_ws != nullptr) ? right_ws : &local_right_ws;

    CompactData_opt(data, len_items, selected_L, size_L, block_size, *LWS);
    CompactData_opt(data, len_items, selected_R, size_R, block_size, *RWS);

    size_t written_L = RecSampling_opt(LWS->data_ptr(), LWS->item_count, k_left, M, block_size,
                                       out, out_capacity, B, bit_offset, LWS, RWS);
                                       
    size_t written_R = RecSampling_opt(RWS->data_ptr(), RWS->item_count, k_right, M, block_size,
                                       out + (written_L * block_size), out_capacity - written_L,
                                       B, bit_offset, LWS, RWS);

    return written_L + written_R;
}
