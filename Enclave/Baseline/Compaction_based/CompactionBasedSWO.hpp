#ifndef BASELINE_COMPACTION_BASED_SWO_HPP
#define BASELINE_COMPACTION_BASED_SWO_HPP

#include <cstddef>
#include "../../../Globals.hpp"
#ifndef BEFTS_MODE
#include "../../Enclave_globals.h"
#endif

// K samples of M records: fresh marks, full compaction, then truncation.
// Mutates all N input records in place; output holds K consecutive samples.
bool CompactionBasedSWO(unsigned char *buffer, size_t N, size_t M, size_t K,
                        size_t block_size, unsigned char *result_buffer,
                        enc_ret *ret);

extern "C" void DecCompactionBasedSWO(unsigned char *encrypted_buffer, size_t N,
                                      size_t M, size_t K,
                                      size_t encrypted_block_size,
                                      unsigned char *encrypted_result_buffer,
                                      enc_ret *ret);

#endif
