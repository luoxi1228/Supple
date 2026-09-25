
#ifndef __TIGHTCOMPACTION_HPP__
#define __TIGHTCOMPACTION_HPP__

#include <sgx_tcrypto.h>
#include "../oasm_lib.h"
#include "../utils.hpp"

/*
   OP_TightCompaction Class:

   OP_TightCompact can take an input array of blocks of block_size bytes each,
   with a function isBlockReal to identify if a block in the array is a real/dummy.
   It returns the input array OP_TightCompact-ed, i.e. all the real blocks 
   are moved to the start of the array. 

   Initialize OP_TightCompaction class templated with the T (uintXX_t) type that 
   is sufficient for the use case, i.e. bits of T should be sufficient to 
   uniquely represent each of the N blocks of the buffer that is to be compacted.

   With: N (total number of blocks in the buffer) 
   The oset_function oincrement_function, and ogt_set_flag_function, correspond
   to the oblivious set, increment, gt_set_flag function respectively for the 
   type T (uint16_t, uint32_t, uint64_t), that can be found in oasm_lib.h.
   setFunctionPointers() will set them for those three uintXX_t types, but corresponding
   setFunctionPointers() have to be added for other templates of OP_TightCompaction.

   NOTE: num_threads doesn't work atm, and OP_TightCompaction is limited to a single-
   threaded execution. To support multi-threading the OP_TightCompaction class would 
   have to make OCAlls to outside and then re-enter with the required number of 
   threads.
 */


template <typename T> 
class OP_TightCompaction {
  T N;
  T *LS_distance;

  // Function Pointers to set according to T
  void (*oset_function)(T *dest, T src, uint8_t flag);
  uint8_t (*ogt_set_flag_function)(T value, T value_test);
  void (*oswap_function)(unsigned char *dest, unsigned char *source, uint32_t buffersize, uint8_t flag);

  void compute_LS_distances(unsigned char *buffer_start, uint8_t (*isBlockReal)(unsigned char *block),
      size_t block_size);
  void compute_LS_distances(unsigned char *buffer_start, bool *selected_list, size_t block_size);
  void process_TCN(uint8_t level, unsigned char *buffer_start, size_t block_size);
  void setFunctionPointers(size_t block_size);

  public:
  void OP_TightCompact(unsigned char *buf, size_t block_size, 
      uint8_t (*isBlockReal)(unsigned char *block));
  void OP_TightCompact(unsigned char *buf, size_t block_size, bool *selected_list);
  OP_TightCompaction(T N, size_t block_size);
  ~OP_TightCompaction();
};


template <typename T>
void inline OP_TightCompaction<T>::setFunctionPointers(size_t block_size) {
}


template <>
void inline OP_TightCompaction<uint16_t>::setFunctionPointers(size_t block_size) {
  oset_function = &(oset_uint16_t);
  ogt_set_flag_function = &(ogt_set_flag_uint16_t);
  if(block_size==8){
    oswap_function = &(oswap_buffer_byte);
  } else {
    oswap_function = &(oswap_buffer);
  }
}


template <>
void inline OP_TightCompaction<uint32_t>::setFunctionPointers(size_t block_size) {
  oset_function = &(oset_uint32_t);
  ogt_set_flag_function = &(ogt_set_flag_uint32_t);
  if(block_size==8){
    oswap_function = &(oswap_buffer_byte);
  } else {
    oswap_function = &(oswap_buffer);
  }
}


template <>
void inline OP_TightCompaction<uint64_t>::setFunctionPointers(size_t block_size) {
  oset_function = &(oset_uint64_t);
  ogt_set_flag_function = &(ogt_set_flag_uint64_t);
  if(block_size==8){
    oswap_function = &(oswap_buffer_byte);
  } else {
    oswap_function = &(oswap_buffer);
  }
}



template <typename T>
void OP_TightCompaction<T>::OP_TightCompact(unsigned char *buf, size_t block_size, 
    bool *selected_list){

  //To support multi-threaded:
  //  if(num_threads>1):
  //    OCALL to untrusted to reaccess this with function with multiple threads 
  //
  int TCN_l = calculatelog2(N);
  compute_LS_distances(buf, selected_list, block_size);
    
  for(int l=0; l<TCN_l; l++) {
    process_TCN(l, buf, block_size); 
  } 
}

template <typename T>
void OP_TightCompaction<T>::OP_TightCompact(unsigned char *buf, size_t block_size, 
    uint8_t (*isBlockReal)(unsigned char *block)){

  //To support multi-threaded:
  //  if(num_threads>1):
  //    OCALL to untrusted to reaccess this with function with multiple threads 
  //
  int TCN_l = calculatelog2(N);
  compute_LS_distances(buf, isBlockReal, block_size);
    
  for(int l=0; l<TCN_l; l++) {
    process_TCN(l, buf, block_size); 
  } 

}

template <typename T>
OP_TightCompaction<T>::OP_TightCompaction(T N_val, size_t block_size) {
  N = N_val;
  LS_distance = new T [N] {0};

  // Set function pointers
  setFunctionPointers(block_size);
}

template <typename T> 
OP_TightCompaction<T>::~OP_TightCompaction() {
  delete [] LS_distance;
}

template <typename T> 
void OP_TightCompaction<T>::compute_LS_distances(unsigned char *buffer_start,
    uint8_t (*isBlockReal)(unsigned char *block), size_t block_size){

  //rp_end = index in the bucket where the current last real packet is mapped to
  T rp_end = 0;
  unsigned char *buffer_ptr = buffer_start;

  // Linear scan over packets of input bucket while updating LS_distance with distance to left shift  
  for(T k=0; k<N; k++) {

    uint8_t real_flag = !(isBlockReal(buffer_ptr));
    T shift_distance = k-rp_end;

    // Oblivious: If real_flag: ls_distance[k]=shift_distance
    //                          rp_end=rp_end+1
    oset_function(&(LS_distance[k]), shift_distance, real_flag); 
    rp_end+=real_flag;

    buffer_ptr+=block_size;
  } 
}


template <typename T> 
void OP_TightCompaction<T>::compute_LS_distances(unsigned char *buffer_start,
    bool *selected_list, size_t block_size){

  //rp_end = index in the bucket where the current last real packet is mapped to
  T rp_end = 0;
  unsigned char *buffer_ptr = buffer_start;

  // Linear scan over packets of input bucket while updating LS_distance with distance to left shift  
  for(T k=0; k<N; k++) {

    uint8_t real_flag = (selected_list[k]==1);
    T shift_distance = k-rp_end;

    // Oblivious: If real_flag: ls_distance[k]=shift_distance
    //                          rp_end=rp_end+1
    oset_function(&(LS_distance[k]), shift_distance, real_flag); 
    rp_end+=real_flag;

    buffer_ptr+=block_size;
  }
}

// Perform the oswaps for input level over the OP_Tight Compaction Network
template <typename T>
void OP_TightCompaction<T>::process_TCN(uint8_t level, unsigned char *bfr_ptr, size_t block_size) { 
  T comparator_dist = pow(2,level);
  // bfr_fop = bfr_first_operand_pointer, bfr_sop = bfr_second_operand_pointer
  unsigned char *bfr_fop = bfr_ptr;
  unsigned char *bfr_sop = bfr_ptr + (comparator_dist * block_size);

  // Number of oblivious swaps
  T num_oswaps = N - comparator_dist;
  T sop_index = comparator_dist;
  T fop_index = 0;

  for(T i=0; i<num_oswaps; i++){ 
    T move_dist = LS_distance[sop_index] & (1 << (level+1)-1);
    // Obliviously if sop!=dummy AND move_dist!=0, set move_flag
    uint8_t dist_flag = ogt_set_flag(move_dist, 0);
    // TODO: Note currently using oswap_buffer_byte, slower than oswap_buffer, 
    // but appropriate for 8 bytes oswaps.
    oswap_function(bfr_fop, bfr_sop, block_size, dist_flag);

    // Adjust LS_distance after an oswap based on move_dist:
    // Obliviously if dist_flag, set LS_distance[thread][fop_index] to 
    // (LS_distance[thread][sop_index]-move_dist)
    LS_distance[sop_index]-= move_dist;
    oset_function(&(LS_distance[fop_index]), LS_distance[sop_index], dist_flag);
    oset_function(&(LS_distance[sop_index]), 0, dist_flag);

    bfr_fop+=block_size;
    bfr_sop+=block_size;
    sop_index++;
    fop_index++;
  }
}
#endif
