#ifndef __OLIB_HPP__
#define __OLIB_HPP__

  #include <stddef.h>
  #include <stdint.h>

  void OLib_initialize();

  void Enclave_loadTestKeys(unsigned char inkey[16], unsigned char outkey[16]);

  // primitive: 0 = OSwap, 1 = OFork.
  // Returns enclave-side elapsed time in microseconds, or -1.0 on failure.
  double MeasureObliviousPrimitive(unsigned char *buf,
                                   size_t pairs,
                                   size_t block_size,
                                   uint8_t primitive,
                                   uint8_t flag0,
                                   uint8_t flag1);

  // Old MT test function
  //void Enclave_testMT();

#endif
