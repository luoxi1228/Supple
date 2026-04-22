#ifndef __TC_HPP__
#define __TC_HPP__

  double testTightCompaction(unsigned char *buffer, size_t N, size_t block_size, size_t nthreads, bool *selected_list, enc_ret *ret);
  double testOPTightCompaction(unsigned char *buffer, size_t N, size_t block_size, bool *selected_list, enc_ret *ret);

#endif
