#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace clusterrun::bench {

// Monotonic wall-clock microseconds for timing bench phases.
inline std::uint64_t now_us() {
  using namespace std::chrono;
  return static_cast<std::uint64_t>(
      duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}

// Reads a positive integer environment variable, or returns fallback.
std::uint32_t env_u32(const char* key, std::uint32_t fallback);

}  // namespace clusterrun::bench
