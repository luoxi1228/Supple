#ifndef __UNTRUSTED_FMS_OFORK_HPP__
#define __UNTRUSTED_FMS_OFORK_HPP__

#include <stddef.h>
#include <stdint.h>

// Prepare and retain the secret FMS control tape inside the enclave.
// Returns 0 on success and a negative value on failure.
int FMSPrepare(const uint8_t *routing_tags,
               size_t n,
               size_t *control_words,
               double *control_bits_us);

// Enclave-side online times in microseconds. Input restoration and ECALL
// transition costs are deliberately excluded from these values.
double FMSApplyOnline(unsigned char *buffer,
                      size_t n,
                      size_t block_size);

double TwoCompactOnline(unsigned char *left_buffer,
                        unsigned char *right_buffer,
                        size_t n,
                        size_t block_size);

void FMSRelease(void);

#endif
