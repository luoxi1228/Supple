#ifndef __NOP_TIGHTCOMPACTION_V2_TCC__
#define __NOP_TIGHTCOMPACTION_V2_TCC__

#include "pthread.h"

/*
   TightCompaction (Non-Order Preserving Tight Compaction):

   Non-Order Preserving TightCompaction can take an input array of blocks of
   block_size bytes each, and an array of "marked" elements with ones at the
   indices corresponding to the blocks that need to be compacted.
   It returns the input array TightCompact-ed, i.e. all the real blocks are
   moved to the start of the array, or compacted to an input index
   (with wraparound)

 */

template <OSwap_Style oswap_style>
void TightCompact_2power(unsigned char *buf, size_t N, size_t block_size, size_t offset, bool *selected) {
  // Compute counts of selected items at power-of-two intervals
  FOAV_SAFE2_CNTXT(TC_2power_summing_selected_count, N, block_size)
  uint32_t *selected_count = NULL;
  FOAV_SAFE_CNTXT(TC_2power, TC_PRECOMPUTE_COUNTS)
  if (TC_PRECOMPUTE_COUNTS) {
    // Allocate array to hold counts
    selected_count = new uint32_t[N+1];
    selected_count[0] = 0;
    // Compute cumulative counts
    for (size_t i=0; i<N; i++){
      FOAV_SAFE2_CNTXT(TC_2power_summing_selected_count, i, N)
      selected_count[i+1] = selected[i] + selected_count[i];
    }
    TightCompact_2power_inner<oswap_style>(buf, N, block_size, offset, selected, selected_count);
    delete[] selected_count;
  } else {
    TightCompact_2power_inner<oswap_style>(buf, N, block_size, offset, selected, selected_count);
  }
}

template <OSwap_Style oswap_style>
void TightCompact_2power_inner(unsigned char *buf, size_t N, size_t block_size, size_t offset, bool *selected, uint32_t *selected_count) {
  FOAV_SAFE2_CNTXT(TC_inner_base_cases_of_recursion, N, block_size)
  if (N==1) {
    return;
  }
  if (N==2) {
    bool swap = (!selected[0] & selected[1]) ^ offset;
    oswap_buffer<oswap_style>(buf, buf+block_size, block_size, swap);
    return;
  }

  // Number of selected items in left half
  size_t m1;
  FOAV_SAFE_CNTXT(TC_2power, TC_PRECOMPUTE_COUNTS)
  if (TC_PRECOMPUTE_COUNTS) {
    m1 = selected_count[N/2] - selected_count[0];
  } else {
    m1=0;
    FOAV_SAFE_CNTXT(TC_2power, N)
    for(size_t i=0; i<N/2; i++){
      FOAV_SAFE_CNTXT(TC_2power, i)
      m1+=selected[i];
    }
  }

  size_t offset_mod = (offset & ((N/2)-1));
  size_t offset_m1_mod = (offset+m1) & ((N/2)-1);
  bool offset_right = (offset >= N/2);
  bool left_wrapped = ((offset_mod + m1) >= (N/2));

  TightCompact_2power_inner<oswap_style>(buf, N/2, block_size, offset_mod, selected, selected_count);
  TightCompact_2power_inner<oswap_style>(buf + ((N/2)*block_size), N/2, block_size, offset_m1_mod, (selected + (N/2)), selected_count + N/2);

  unsigned char *buf1_ptr = buf, *buf2_ptr = (buf + (N/2)*block_size);
  FOAV_SAFE_CNTXT(TC_2power_inner, TC_OPT_SWAP_FLAG)
  if (TC_OPT_SWAP_FLAG) {
    bool swap_flag = left_wrapped ^ offset_right;
    size_t num_swap = N/2;
    FOAV_SAFE2_CNTXT(TC_2power_inner, num_swap, block_size)
    for(size_t i=0; i<num_swap; i++){
      FOAV_SAFE2_CNTXT(TC_2power_inner_N/2_swaps, i, num_swap)
      swap_flag = swap_flag ^ (i == offset_m1_mod);
      oswap_buffer<oswap_style>(buf1_ptr, buf2_ptr, block_size, swap_flag);
      buf1_ptr+=block_size;
      buf2_ptr+=block_size;
      FOAV_SAFE2_CNTXT(TC_2power_inner, num_swap, block_size)
    }
  } else {
    FOAV_SAFE_CNTXT(TC_2power_inner, N)
    for(size_t i=0; i<N/2; i++){
      FOAV_SAFE_CNTXT(TC_2power_inner, i)
      bool swap_flag = (i>=offset_m1_mod) ^ left_wrapped ^ offset_right;
      oswap_buffer<oswap_style>(buf1_ptr, buf2_ptr, block_size, swap_flag);
      buf1_ptr+=block_size;
      buf2_ptr+=block_size;
      FOAV_SAFE2_CNTXT(TC_2power_inner, i, N)
    }
  }
}

struct TightCompact_2power_inner_parallel_args {
    unsigned char *buf;
    size_t N, block_size, offset;
    bool *selected;
    uint32_t *selected_count;
    size_t nthreads;
};

template <OSwap_Style oswap_style>
static void* TightCompact_2power_inner_parallel_launch(void *voidargs) {
    struct TightCompact_2power_inner_parallel_args *args =
        (TightCompact_2power_inner_parallel_args *)voidargs;
    TightCompact_2power_inner_parallel<oswap_style>(args->buf, args->N,
        args->block_size, args->offset, args->selected, args->selected_count,
        args->nthreads);
    return NULL;
}

struct oswap_range_args {
    size_t block_size;
    size_t swap_start, swap_end;
    size_t offset_m1_mod;
    unsigned char *buf1, *buf2;
    bool swap_flag;
};

template <OSwap_Style oswap_style>
static void* oswap_range(void *voidargs) {
    struct oswap_range_args *args = (oswap_range_args*)voidargs;
    size_t block_size = args->block_size;
    size_t swap_start = args->swap_start;
    size_t swap_end = args->swap_end;
    size_t offset_m1_mod = args->offset_m1_mod;
    unsigned char *buf1 = args->buf1 + swap_start*block_size;
    unsigned char *buf2 = args->buf2 + swap_start*block_size;
    bool swap_flag = args->swap_flag;
    FOAV_SAFE2_CNTXT(oswap_range, block_size, swap_start)
    FOAV_SAFE_CNTXT(oswap_range, swap_end)
    //FOAV_SAFE_CNTXT(oswap_range, &OSWAP_COUNTER)
    //printf("start oswap range %p %lu %lu\n", buf1, swap_start, swap_end);
    for(size_t i=swap_start; i<swap_end; i++){
      FOAV_SAFE2_CNTXT(oswap_range, i, swap_end)
      oswap_buffer<oswap_style>(buf1, buf2, block_size, swap_flag ^ (i >= offset_m1_mod));
      buf1+=block_size;
      buf2+=block_size;
      //FOAV_SAFE_CNTXT(oswap_range, &OSWAP_COUNTER)
      FOAV_SAFE2_CNTXT(oswap_range, swap_end, block_size)
      FOAV_SAFE_CNTXT(oswap_range, i)
    }
    //printf("end oswap range %p %lu %lu\n", buf1, swap_start, swap_end);
    return NULL;
}


template <OSwap_Style oswap_style>
void TightCompact_2power_inner_parallel(unsigned char *buf, size_t N, size_t block_size, size_t offset, bool *selected, uint32_t *selected_count, size_t nthreads) {
  FOAV_SAFE_CNTXT(TC_inner_base_cases_of_recursion, g_thread_id)
  FOAV_SAFE2_CNTXT(TC_inner_base_cases_of_recursion, N, block_size)
  FOAV_SAFE_CNTXT(TC_inner_base_cases_of_recursion, nthreads)
  if (nthreads <= 1) {
    FOAV_SAFE2_CNTXT(TC_inner_base_cases_of_recursion, N, block_size)
    FOAV_SAFE_CNTXT(TC_inner_base_cases_of_recursion, nthreads)
    unsigned long start = printf_with_rtclock("Thread %u starting TightCompact_2power_inner(buf=%p, N=%lu, offset=%lu, nthreads=%lu)\n", g_thread_id, buf, N, offset, nthreads);
    TightCompact_2power_inner<oswap_style>(buf, N, block_size, offset, selected, selected_count);
    printf_with_rtclock_diff(start, "Thread %u ending TightCompact_2power_inner(buf=%p, N=%lu, offset=%lu, nthreads=%lu)\n", g_thread_id, buf, N, offset, nthreads);
    return;
  }
  FOAV_SAFE_CNTXT(TC_inner_base_cases_of_recursion, N)
  if (N==1) {
    return;
  }
  FOAV_SAFE_CNTXT(TC_inner_base_cases_of_recursion, N)
  if (N==2) {
    bool swap = (!selected[0] & selected[1]) ^ offset;
    oswap_buffer<oswap_style>(buf, buf+block_size, block_size, swap);
    return;
  }

  unsigned long start = printf_with_rtclock("Thread %u starting TightCompact_2power_inner_parallel(buf=%p, N=%lu, offset=%lu, nthreads=%lu)\n", g_thread_id, buf, N, offset, nthreads);
  // Number of selected items in left half
  size_t m1;
  m1 = selected_count[N/2] - selected_count[0];

  size_t offset_mod = (offset & ((N/2)-1));
  size_t offset_m1_mod = (offset+m1) & ((N/2)-1);
  bool offset_right = (offset >= N/2);
  bool left_wrapped = ((offset_mod + m1) >= (N/2));
  size_t lthreads = nthreads/2;
  size_t rthreads = nthreads - lthreads;

  threadid_t rightthreadid = g_thread_id + lthreads;
  /* Dispatch the right half to thread g_thread_id + lthreads; it will inherit threads
     g_thread_id + lthreads .. g_thread_id + nthreads-1. */
  struct TightCompact_2power_inner_parallel_args rightargs = {
    buf+ ((N/2)*block_size), N/2, block_size, offset_m1_mod, selected + N/2, selected_count + N/2, rthreads
  };
  threadpool_dispatch(rightthreadid,
    TightCompact_2power_inner_parallel_launch<oswap_style>,
    &rightargs);
  /* Do the left half ourselves (threads g_thread_id .. g_thread_id + lthreads-1) */
  TightCompact_2power_inner_parallel<oswap_style>(buf, N/2, block_size, offset_mod, selected, selected_count, lthreads);
  threadpool_join(rightthreadid, NULL);

  unsigned char *buf1_ptr = buf, *buf2_ptr = (buf + (N/2)*block_size);
  bool swap_flag = left_wrapped ^ offset_right;
  size_t num_swap = N/2;
  FOAV_SAFE2_CNTXT(TC_2power_inner, num_swap, block_size)

  oswap_range_args args[nthreads];
  size_t inc = num_swap / nthreads;
  size_t extra = num_swap % nthreads;
  size_t last = 0;
  for (size_t i=0; i<nthreads; ++i) {
    size_t next = last + inc + (i < extra);
    args[i] = { block_size, last, next, offset_m1_mod, buf1_ptr, buf2_ptr, swap_flag };
    last = next;
  }
  for (size_t i=0; i<nthreads-1; ++i) {
    threadpool_dispatch(g_thread_id+1+i, oswap_range<oswap_style>, args+i);
  }
  // Do the last section ourselves
  oswap_range<oswap_style>((void*)(args+nthreads-1));
  for (size_t i=0; i<nthreads-1; ++i) {
    FOAV_SAFE2_CNTXT(TC_2power_inner_parallel, i, nthreads)
    threadpool_join(g_thread_id+1+i, NULL);
  }
  printf_with_rtclock_diff(start, "Thread %u ending TightCompact_2power_inner_parallel(buf=%p, N=%lu, offset=%lu, nthreads=%lu)\n", g_thread_id, buf, N, offset, nthreads);
}

/*
  NOTE: TightCompact can only be invoked with offset 0.
  To invoke with a non-0 offset, use TightCompact_2power, with N = 2^x.
*/

template <OSwap_Style oswap_style>
void TightCompact(unsigned char *buf, size_t N, size_t block_size,
       bool *selected) {
  FOAV_SAFE2_CNTXT(TC_inner_base_cases_of_recursion, N, block_size)
  uint32_t *selected_count = NULL;
  if (TC_PRECOMPUTE_COUNTS) {

    // Allocate array to hold counts 
    try {
      selected_count = new uint32_t[N+1];
    } catch (std::bad_alloc&){
      printf("Allocating memory failed in TC\n");
    }
    selected_count[0] = 0;

    // Compute cumulative counts
    for (size_t i=0; i<N; i++){
      selected_count[i+1] = selected[i] + selected_count[i];
    }
    TightCompact_inner<oswap_style>(buf, N, block_size, selected, selected_count);
    delete[] selected_count;
  } else {
    TightCompact_inner<oswap_style>(buf, N, block_size, selected, selected_count);
  }
}

template <OSwap_Style oswap_style>
void TightCompact_parallel(unsigned char *buf, size_t N, size_t block_size,
       bool *selected, size_t nthreads) {
  FOAV_SAFE2_CNTXT(TC_inner_base_cases_of_recursion, N, block_size)
  uint32_t *selected_count = NULL;
  // Allocate array to hold counts
  try {
    selected_count = new uint32_t[N+1];
  } catch (std::bad_alloc&){
    printf("Allocating memory failed in TC\n");
  }
  selected_count[0] = 0;

  // Compute cumulative counts
  for (size_t i=0; i<N; i++){
    selected_count[i+1] = selected[i] + selected_count[i];
  }
  //printf("TightCompact_parallel(nthreads=%lu)\n", nthreads);
  TightCompact_inner_parallel<oswap_style>(buf, N, block_size, selected, selected_count, nthreads);
  delete[] selected_count;
}

template <OSwap_Style oswap_style>
void TightCompact_inner(unsigned char *buf, size_t N, size_t block_size, bool *selected, uint32_t *selected_count){
  FOAV_SAFE2_CNTXT(TC_inner_base_cases_of_recursion, N, block_size)
  if(N==0){
    return;
  }
  else if(N==1){
    return;
  }
  else if(N==2){
    bool swap = (!selected[0] & selected[1]);
    oswap_buffer<oswap_style>(buf, buf+block_size, block_size, swap);
    return;
  }

  size_t gt_pow2;
  size_t split_index;

  // Find largest power of 2 < N 
  gt_pow2 = pow2_lt(N);

  // For Order-preserving ORCompact
  // This will be right (R) of the recursion, and the leftover non-power of 2 left (L)
  split_index = N - gt_pow2;

  // Number of selected items in the non-power of 2 side (left)
  size_t mL;
  if (TC_PRECOMPUTE_COUNTS) {
    mL = selected_count[split_index] - selected_count[0];
  } else {
    mL = 0;
    for(size_t i=0; i<split_index; i++){
      mL+=selected[i];
    }
  }

  unsigned char *L_ptr = buf;
  unsigned char *R_ptr = buf + (split_index * block_size);

  //printf("Lsize = %ld, Rsize = %ld, Rside offset = %ld\n", split_index, gt_pow2, (gt_pow2 - split_index + mL));
  TightCompact_inner<oswap_style>(L_ptr, split_index, block_size, selected, selected_count);
  TightCompact_2power_inner<oswap_style>(R_ptr, gt_pow2, block_size, (gt_pow2 - split_index + mL) % gt_pow2, selected+split_index, selected_count+split_index);

  // For OP we CnS the first n_2 elements (split_size) against the suffix n_2 elements of the n_1 (2 power elements)
  R_ptr = buf + (gt_pow2 * block_size); 

  // Perform N-split_index oblivious swaps for this level
  FOAV_SAFE_CNTXT(TC_inner_oswap_loop, split_index)
  for (size_t i=0; i<split_index; i++){
    FOAV_SAFE2_CNTXT(TC_inner_oswap_loop, i, split_index)
    // Oswap blocks at L_start, R_start conditional on marked_items
    bool swap_flag = i>=mL;
    oswap_buffer<oswap_style>(L_ptr, R_ptr, block_size, swap_flag);
    L_ptr+=block_size;
    R_ptr+=block_size;
    FOAV_SAFE2_CNTXT(TC_inner_oswap_loop, i, split_index)
  }
}


template <OSwap_Style oswap_style>
void TightCompact_inner_parallel(unsigned char *buf, size_t N, size_t block_size, bool *selected, uint32_t *selected_count, size_t nthreads){
  FOAV_SAFE2_CNTXT(TC_inner_base_cases_of_recursion, N, block_size)
  FOAV_SAFE_CNTXT(TC_inner_base_cases_of_recursion, nthreads)
  if (nthreads <= 1 || N < 16) {
    unsigned long start = printf_with_rtclock("Thread %u starting TightCompact_inner(N=%lu)\n", g_thread_id, N);
    TightCompact_inner<oswap_style>(buf, N, block_size, selected, selected_count);
    printf_with_rtclock_diff(start, "Thread %u ending TightCompact_inner(N=%lu)\n", g_thread_id, N);
    return;
  }
  if(N==0){
    return;
  }
  else if(N==1){
    return;
  }
  else if(N==2){
    bool swap = (!selected[0] & selected[1]);
    oswap_buffer<oswap_style>(buf, buf+block_size, block_size, swap);
    return;
  }

  unsigned long start = printf_with_rtclock("Thread %u starting TightCompact_inner_parallel(N=%lu, nthreads=%lu)\n", g_thread_id, N, nthreads);

  size_t split_index, n1, n2;

  // Find largest power of 2 < N
  // This will be right (R) n1 of the recursion, and the leftover left (L) n2
  n1 = pow2_lt(N);
  n2 = N - n1;

  // Number of selected items in left
  size_t m2;
  m2 = selected_count[n2] - selected_count[0];

  unsigned char *L_ptr = buf;
  unsigned char *R_ptr = buf + (n2 * block_size);

  size_t lthreads = nthreads/2;
  size_t rthreads = nthreads - lthreads;

  struct TightCompact_2power_inner_parallel_args rightargs = {
    R_ptr, n1, block_size, n1 - n2 + m2, selected + n2, selected_count + n2,
    rthreads
  };
  threadpool_dispatch(g_thread_id+lthreads,
    TightCompact_2power_inner_parallel_launch<oswap_style>,
    &rightargs);
  TightCompact_inner_parallel<oswap_style>(L_ptr, n2, block_size, selected, selected_count, lthreads);
  threadpool_join(g_thread_id+lthreads, NULL);

  size_t num_swap = N-n1;
  FOAV_SAFE2_CNTXT(TC_inner_parallel, nthreads, num_swap)
  oswap_range_args args[nthreads];
  size_t inc = num_swap / nthreads;
  size_t extra = num_swap % nthreads;
  size_t last = 0;

  // We tweak R_ptr before we compare, to set compare the n2 prefix in L with the n2 suffix of R
  R_ptr = buf + (n1 * block_size);

  for (size_t i=0; i<nthreads; ++i) {
    size_t next = last + inc + (i < extra);
    args[i] = { block_size, last, next, m2, L_ptr, R_ptr, false };
    last = next;
    FOAV_SAFE2_CNTXT(TC_inner_parallel, i, nthreads)
  }
  for (size_t i=0; i<nthreads-1; ++i) {
    threadpool_dispatch(g_thread_id+1+i, oswap_range<oswap_style>, args+i);
  }
  // Do the last section ourselves
  oswap_range<oswap_style>((void*)(args+nthreads-1));
  FOAV_SAFE_CNTXT(TC_inner_parallel, nthreads)
  for (size_t i=0; i<nthreads-1; ++i) {
    threadpool_join(g_thread_id+1+i, NULL);
    FOAV_SAFE2_CNTXT(TC_inner_parallel, i, nthreads)
  }

  printf_with_rtclock_diff(start, "Thread %u ending TightCompact_inner_parallel(N=%lu, nthreads=%lu)\n", g_thread_id, N, nthreads);
}

  #ifndef BEFTS_MODE 

  // Perform the oswaps for input level over the OP_Tight Compaction Network
  template <OSwap_Style oswap_style>
  void process_TCN(uint64_t N, uint8_t level, unsigned char *bfr_ptr, size_t block_size,
          uint64_t *LS_distance) { 
    FOAV_SAFE2_CNTXT(process_TCN, N, level)
    FOAV_SAFE_CNTXT(process_TCN, block_size)
    uint64_t comparator_dist = (1<<level);
    // bfr_fop = bfr_first_operand_pointer, bfr_sop = bfr_second_operand_pointer
    unsigned char *bfr_fop = bfr_ptr;
    unsigned char *bfr_sop = bfr_ptr + (comparator_dist * block_size);

    // Number of oblivious swaps
    uint64_t num_oswaps = N - comparator_dist;
    uint64_t sop_index = comparator_dist;
    uint64_t fop_index = 0;

    FOAV_SAFE_CNTXT(process_TCN, num_oswaps)
    for(uint64_t i=0; i<num_oswaps; i++){ 
      uint64_t move_dist = LS_distance[sop_index] & (1 << (level+1)-1);
      // Obliviously if sop!=dummy AND move_dist!=0, set move_flag
      uint8_t dist_flag = ogt_set_flag(move_dist, 0);
      // but appropriate for 8 bytes oswaps.
      oswap_buffer<oswap_style>(bfr_fop, bfr_sop, block_size, dist_flag);

      // Adjust LS_distance after an oswap based on move_dist:
      // Obliviously if dist_flag, set LS_distance[thread][fop_index] to 
      // (LS_distance[thread][sop_index]-move_dist)
      LS_distance[sop_index]-= move_dist;
      oset_value(&(LS_distance[fop_index]), LS_distance[sop_index], dist_flag);
      oset_value(&(LS_distance[sop_index]), 0, dist_flag);

      bfr_fop+=block_size;
      bfr_sop+=block_size;
      sop_index++;
      fop_index++;
      FOAV_SAFE2_CNTXT(process_TCN, i, num_oswaps)
    }
  }


  template <OSwap_Style oswap_style>
  void OP_TightCompact(unsigned char *buf, uint64_t N, size_t block_size, bool *selected_list){
    
    FOAV_SAFE2_CNTXT(OP_TightCompact, N, block_size)
    FOAV_SAFE2_CNTXT(OP_TightCompact, buf, selected_list)
    uint64_t *LS_distance = new uint64_t[N];

    int TCN_l = calculatelog2(N);
    compute_LS_distances(N, buf, block_size, selected_list, LS_distance);
      
    FOAV_SAFE_CNTXT(OP_TightCompact, TCN_l)
    for(int l=0; l<TCN_l; l++) {
      process_TCN<oswap_style>(N, l, buf, block_size, LS_distance); 
      FOAV_SAFE2_CNTXT(OP_TightCompact, l, TCN_l)
    } 

    delete[] LS_distance;
  }

  #endif

#endif
