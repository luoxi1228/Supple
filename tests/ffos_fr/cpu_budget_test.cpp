#include "../../Untrusted/CpuBudget.hpp"
#include <map>
#include <cassert>
int main() {
  std::map<std::string,std::string> files;
  auto read = [&](const std::string &path) { return files[path]; };
  for (size_t n : {16U,32U,64U}) assert(supple_cpu::AvailableFrom(n, 1, read) == n);
  assert(supple_cpu::AvailableFrom(0,32,read)==32);
  assert(supple_cpu::AvailableFrom(0,-1,read)==1);
  files["/proc/self/cgroup"]="0::/a/b\n";
  files["/proc/self/mountinfo"]="1 0 0:1 / /cg rw - cgroup2 cgroup rw\n";
  files["/cg/a/cpu.max"]="150000 100000";
  assert(supple_cpu::AvailableFrom(64,1,read)==2);
  assert(supple_cpu::AvailableFrom(1,1,read)==1);
  files.clear();
  files["/proc/self/cgroup"]="1:cpu,cpuacct:/a\n";
  files["/proc/self/mountinfo"]="1 0 0:1 / /cg rw - cgroup cgroup rw,cpu,cpuacct\n";
  files["/cg/a/cpu.cfs_quota_us"]="1700000";
  files["/cg/a/cpu.cfs_period_us"]="100000";
  assert(supple_cpu::AvailableFrom(64,1,read)==17);
  assert(supple_cpu::Quota("max 100000")==0);
  assert(supple_cpu::Quota("-1 100000")==0);
  printf("PASS: C++ CPU probe mocks; current available CPUs: %zu\n",supple_cpu::Available());
}
