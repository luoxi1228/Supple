#ifndef __RECURSIVESHUFFLE_TCC__
#define __RECURSIVESHUFFLE_TCC__

#ifndef BEFTS_MODE
  #include "../ObliviousPrimitives.hpp"
#endif

template<OSwap_Style oswap_style>
void RecursiveShuffle_M2_inner(unsigned char *buf, uint64_t N, size_t block_size, bool *selected_list) {
  // Base cases
  FOAV_SAFE_CNTXT(RecursiveShuffle_M2_inner, N)
  if (N <= 1) {
      return;
  }
  FOAV_SAFE_CNTXT(RecursiveShuffle_M2_inner, N)
  if (N == 2) {
      //Flip a coin and swap the two
      unsigned char *packet_1 = buf;
      unsigned char *packet_2 = buf + block_size;
      bool swap_flag = getRandomBit();
      oswap_buffer<oswap_style>(packet_1, packet_2, block_size, swap_flag);
      return;
  }

  // MarkHalf the elements
  MarkHalf(N, selected_list);

  //TightCompact
  TightCompact<oswap_style>(buf, N, block_size, selected_list);

  // Recursively shuffle each half
  size_t lsize = N/2;
  size_t rsize = N - lsize;
  bool *selected_L = selected_list;
  bool *selected_R = selected_list + lsize; 

  RecursiveShuffle_M2_inner<oswap_style>(buf, lsize, block_size, selected_L);
  RecursiveShuffle_M2_inner<oswap_style>(buf+ lsize*block_size, rsize, block_size, selected_R);
}

struct RecursiveShuffle_M2_inner_parallel_args {
    unsigned char *buf;
    uint64_t N;
    size_t block_size;
    bool *selected_list;
    size_t nthreads;
};

template<OSwap_Style oswap_style>
void RecursiveShuffle_M2_inner_parallel(unsigned char *buf, uint64_t N, size_t block_size, bool *selected_list, size_t nthreads);

template<OSwap_Style oswap_style>
static void* RecursiveShuffle_M2_inner_parallel_launch(void *voidargs) {
    struct RecursiveShuffle_M2_inner_parallel_args *args =
        (RecursiveShuffle_M2_inner_parallel_args*)voidargs;
    RecursiveShuffle_M2_inner_parallel<oswap_style>(args->buf, args->N, args->block_size,
        args->selected_list, args->nthreads);
    return NULL;
}

template<OSwap_Style oswap_style>
void RecursiveShuffle_M2_inner_parallel(unsigned char *buf, uint64_t N, size_t block_size, bool *selected_list, size_t nthreads) {
  FOAV_SAFE2_CNTXT(RS_M2_inner_parallel, nthreads, N)
  if (nthreads <= 1) {
#ifdef VERBOSE_TIMINGS_RECSHUFFLE
    unsigned long start = printf_with_rtclock("Thread %u starting RecursiveShuffle_M2_inner(N=%lu)\n", g_thread_id, N);
#endif
    RecursiveShuffle_M2_inner<oswap_style>(buf, N, block_size, selected_list);
#ifdef VERBOSE_TIMINGS_RECSHUFFLE
    printf_with_rtclock_diff(start, "Thread %u ending RecursiveShuffle_M2_inner(N=%lu)\n", g_thread_id, N);
#endif
    return;
  }
  // Base cases
  FOAV_SAFE_CNTXT(RecursiveShuffle_M2_inner, N)
  if (N <= 1) {
      return;
  }
  FOAV_SAFE_CNTXT(RecursiveShuffle_M2_inner, N)
  if (N == 2) {
      //Flip a coin and swap the two
      unsigned char *packet_1 = buf;
      unsigned char *packet_2 = buf + block_size;
      bool swap_flag = getRandomBit();
      oswap_buffer<oswap_style>(packet_1, packet_2, block_size, swap_flag);
      return;
  }
#ifdef VERBOSE_TIMINGS_RECSHUFFLE
  unsigned long start = printf_with_rtclock("Thread %u starting RecursiveShuffle_M2_inner_parallel(N=%lu, nthreads=%lu)\n", g_thread_id, N, nthreads);
#endif

  printf("Before MarkHalf\n");
  // MarkHalf the elements
  MarkHalf(N, selected_list);
  printf("After MarkHalf\n");

  //TightCompact
  TightCompact_parallel<oswap_style>(buf, N, block_size, selected_list, nthreads);

  // Recursively shuffle each half
  size_t lsize = N/2;
  size_t rsize = N - lsize;
  bool *selected_L = selected_list;
  bool *selected_R = selected_list + lsize;
#if 1
  size_t lthreads = nthreads/2;
  size_t rthreads = nthreads - lthreads;

  /* The left half will be processed by thread g_thread_id + rthreads, which will
   * inherit threads g_thread_id+rthreads .. g_thread_id + nthreads-1. */
  threadid_t leftthreadid = g_thread_id + rthreads;
  RecursiveShuffle_M2_inner_parallel_args leftargs = {
    buf, lsize, block_size, selected_L, lthreads
  };
  threadpool_dispatch(leftthreadid,
    RecursiveShuffle_M2_inner_parallel_launch<oswap_style>,
    &leftargs);
  /* We will do the right half ourselves, on threads g_thread_id .. g_thread_id..rthreads-1 */
  RecursiveShuffle_M2_inner_parallel<oswap_style>(buf+ lsize*block_size, rsize, block_size, selected_R, rthreads);
  threadpool_join(leftthreadid, NULL);
#else
  RecursiveShuffle_M2_inner_parallel<oswap_style>(buf, lsize, block_size, selected_L, nthreads);
  RecursiveShuffle_M2_inner_parallel<oswap_style>(buf+ lsize*block_size, rsize, block_size, selected_R, nthreads);
#endif
#ifdef VERBOSE_TIMINGS_RECSHUFFLE
  printf_with_rtclock_diff(start, "Thread %u ending RecursiveShuffle_M2_inner_parallel(N=%lu, nthreads=%lu)\n", g_thread_id, N, nthreads);
#endif
}


#ifndef BEFTS_MODE
  template<OSwap_Style oswap_style>
  void RecursiveShuffle_M1_inner(unsigned char *buf, uint64_t N, size_t block_size, bool *selected_list) {
    FOAV_SAFE2_CNTXT(RS_M1_inner, N, block_size)
    // Base cases
    if (N == 1) {
        return;
    }
    FOAV_SAFE2_CNTXT(RS_M1_inner, N, block_size)
    if (N == 2) {
        //Flip a coin and swap the two
        unsigned char *packet_1 = buf;
        unsigned char *packet_2 = buf + block_size;
        bool swap_flag = getRandomBit();
        oswap_buffer<oswap_style>(packet_1, packet_2, block_size, swap_flag);
        return;
    }

    // MarkHalf the elements
    MarkHalf(N, selected_list);

    //TightCompact
    OP_TightCompact<oswap_style>(buf, N, block_size, selected_list);

    // Recursively shuffle each half
    size_t l_size = N/2;
    size_t r_size = N-l_size;
    bool *selected_L = selected_list;
    bool *selected_R = selected_list + l_size; 
    RecursiveShuffle_M1_inner<oswap_style>(buf, l_size, block_size, selected_L);
    RecursiveShuffle_M1_inner<oswap_style>(buf+(l_size*block_size), r_size, block_size, selected_R);

  }
#endif
#endif
