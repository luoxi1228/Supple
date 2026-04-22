#include "TightCompaction_v2.hpp"


void compute_LS_distances(uint64_t N, unsigned char *buffer_start,
      size_t block_size, bool *selected_list, uint64_t *LS_distance){

  //rp_end = index in the bucket where the current last real packet is mapped to
  uint64_t rp_end = 0;
  unsigned char *buffer_ptr = buffer_start;

  // Linear scan over packets of input bucket while updating LS_distance with distance to left shift  
  FOAV_SAFE2_CNTXT(TC_compute_LS_distances, N, block_size)
  for(uint64_t k=0; k<N; k++) {

    uint8_t real_flag = (selected_list[k]==1);
    uint64_t shift_distance = k-rp_end;

    // Oblivious: If real_flag: ls_distance[k]=shift_distance
    //                          rp_end=rp_end+1
    oset_value(&(LS_distance[k]), shift_distance, real_flag); 
    rp_end+=real_flag;

    buffer_ptr+=block_size;
    FOAV_SAFE2_CNTXT(TC_compute_LS_distances, N, k)
  }
}
