#include "bench/bench_common.hpp"

#include <cstdlib>

namespace clusterrun::bench {

std::uint32_t env_u32(const char* key, std::uint32_t fallback) {
  const char* v = std::getenv(key);
  if (v == nullptr || v[0] == '\0') {
    return fallback;
  }
  return static_cast<std::uint32_t>(std::strtoul(v, nullptr, 10));
}

}  // namespace clusterrun::bench
