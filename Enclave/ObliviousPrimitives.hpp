#ifndef __OBLIVIOUS_PRIMITIVES_HEADER__
  #define __OBLIVIOUS_PRIMITIVES_HEADER__

  #ifndef BEFTS_MODE
    #include "TightCompaction/TightCompaction_v2.hpp"
    // #include "SortingNetwork/SortingNetwork.hpp"
    #include "RecursiveShuffle/RecursiveShuffle.hpp"
  #endif

  // The new APIs that are more efficient in OSWAP_BUFFER calls:
  void TightCompact_v2(unsigned char *buffer, size_t N, size_t block_size, bool *selected_list);
  void TightCompact_v2_parallel(unsigned char *buffer, size_t N, size_t block_size, bool *selected_list, size_t nthreads);

  void OP_TightCompact_v2(unsigned char *buffer, size_t N, size_t block_size, bool *selected_list); 

#endif
