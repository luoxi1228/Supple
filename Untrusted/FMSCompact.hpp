#ifndef UNTRUSTED_FMS_COMPACT_HPP
#define UNTRUSTED_FMS_COMPACT_HPP

#include <stddef.h>
#include <stdint.h>

// Prepare and retain the secret FMS control tape inside the enclave.
// Returns 0 on success and a negative value on failure.
int FMSCompactPrepare(const uint8_t *routing_tags,
                      size_t n,
                      size_t n_left,
                      size_t n_right,
                      size_t *control_words,
                      double *control_us);

// Enclave-side online times in microseconds. Input restoration and ECALL
// transition costs are deliberately excluded from these values.
double FMSCompactOnline(unsigned char *buffer,
                        size_t n,
                        size_t block_size);

double OCompactOnline(unsigned char *buffer,
                      size_t n,
                      size_t block_size);

void FMSCompactRelease(void);

#endif
