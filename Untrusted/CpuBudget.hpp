#ifndef SUPPLE_CPU_BUDGET_HPP
#define SUPPLE_CPU_BUDGET_HPP
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <iterator>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sched.h>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace supple_cpu {
inline std::string Read(const std::string &path) {
  std::ifstream stream(path.c_str());
  return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}
inline size_t Quota(const std::string &quota, const std::string &period = "") {
  std::istringstream stream(period.empty() ? quota : quota + " " + period);
  std::string first;
  unsigned long long q = 0, p = 0;
  std::string second, extra;
  if (!(stream >> first) || first == "max" || first.empty() || first[0] == '-') return 0;
  char *end = nullptr;
  errno = 0;
  q = std::strtoull(first.c_str(), &end, 10);
  if (errno || !end || *end || !(stream >> second) || second.empty() || second[0] == '-' || (stream >> extra)) return 0;
  errno = 0;
  p = std::strtoull(second.c_str(), &end, 10);
  if (errno || !end || *end || !p || !q) return 0;
  const unsigned long long limit = q / p + (q % p != 0);
  return static_cast<size_t>(std::min<unsigned long long>(limit, SIZE_MAX));
}
inline std::string Unescape(std::string path) {
  size_t at = 0;
  while ((at = path.find("\\040", at)) != std::string::npos) path.replace(at, 4, " ");
  return path;
}
inline size_t Affinity() {
  size_t cpus = 0;
  for (size_t bits = 128; bits <= SIZE_MAX / 2; bits *= 2) {
    cpu_set_t *set = CPU_ALLOC(bits);
    if (!set) break;
    const size_t bytes = CPU_ALLOC_SIZE(bits);
    CPU_ZERO_S(bytes, set);
    const int result = sched_getaffinity(0, bytes, set);
    const int error = errno;
    if (result == 0) cpus = CPU_COUNT_S(bytes, set);
    CPU_FREE(set);
    if (result == 0 || error != EINVAL) break;
  }
  return cpus;
}
template<class Reader>
size_t AvailableFrom(size_t affinity, long online, Reader read) {
  size_t cpus = affinity ? affinity : (online > 0 ? static_cast<size_t>(online) : 1);
  std::string unified_group = "/", cpu_group = "/", line;
  std::istringstream groups(read("/proc/self/cgroup"));
  while (std::getline(groups, line)) {
    const size_t a = line.find(':'), b = line.find(':', a + 1);
    if (a == std::string::npos || b == std::string::npos) continue;
    const std::string controllers = "," + line.substr(a + 1, b - a - 1) + ",";
    if (controllers == ",,") unified_group = line.substr(b + 1);
    if (controllers.find(",cpu,") != std::string::npos) cpu_group = line.substr(b + 1);
  }
  auto inspect = [&](std::string path, const std::string &root, bool unified) {
    for (;;) {
      const size_t quota = unified ? Quota(read(path + "/cpu.max")) :
          Quota(read(path + "/cpu.cfs_quota_us"), read(path + "/cpu.cfs_period_us"));
      if (quota) cpus = std::min(cpus, quota);
      if (path == root || path.size() <= root.size()) break;
      const size_t slash = path.rfind('/');
      if (slash == std::string::npos) break;
      path.resize(slash);
    }
  };
  inspect("/sys/fs/cgroup", "/sys/fs/cgroup", true);
  std::istringstream mounts(read("/proc/self/mountinfo"));
  while (std::getline(mounts, line)) {
    const size_t separator = line.find(" - ");
    if (separator == std::string::npos) continue;
    std::istringstream before(line.substr(0, separator)), after(line.substr(separator + 3));
    std::string id, parent, device, root, point, type, source, options;
    if (!(before >> id >> parent >> device >> root >> point) ||
        !(after >> type >> source >> options)) continue;
    const bool unified = type == "cgroup2";
    if (!unified && !(type == "cgroup" && ("," + options + ",").find(",cpu,") != std::string::npos)) continue;
    root = Unescape(root); point = Unescape(point);
    const std::string group = unified ? unified_group : cpu_group;
    std::string relative;
    if (root == "/") relative = group == "/" ? "" : group;
    else if (group == root) relative = "";
    else if (group.compare(0, root.size() + 1, root + "/") == 0) relative = group.substr(root.size());
    inspect(point + relative, point, unified);
  }
  return std::max<size_t>(1, cpus);
}
inline size_t Available() {
  return AvailableFrom(Affinity(), sysconf(_SC_NPROCESSORS_ONLN), Read);
}
}
#endif
