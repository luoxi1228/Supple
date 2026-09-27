#ifndef SUPPLE_ONLINE_PROFILE_HPP
#define SUPPLE_ONLINE_PROFILE_HPP

#ifndef BEFTS_MODE
#include "../utils.hpp"
#endif

namespace online_profile
{

template <typename Operation>
void Track(double *milliseconds, Operation operation)
{
  if (milliseconds == nullptr)
  {
    operation();
    return;
  }
  long start = 0, end = 0;
  ocall_clock(&start);
  operation();
  ocall_clock(&end);
  *milliseconds += static_cast<double>(end - start) / 1000.0;
}

} // namespace online_profile

#endif
