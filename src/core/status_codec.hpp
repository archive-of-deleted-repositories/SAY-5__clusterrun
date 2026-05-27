#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/types.hpp"

namespace clusterrun {

// Wire layout (little-endian), in order:
//   u32 len        length in bytes of everything after this field
//   u64 job_id
//   u8  status
//   u64 ts_us
//   u32 progress
//   u16 worker_id
//   bytes detail   len - (8+1+8+4+2) bytes
//
// The fixed header after len is 23 bytes; detail fills the remainder.
inline constexpr std::size_t kStatusFixedAfterLen = 8 + 1 + 8 + 4 + 2;

// Encodes a status update to its binary frame.
std::vector<std::uint8_t> encode_status(const StatusUpdate& u);

// Decodes a status frame. Returns nullopt if the buffer is truncated, if len is
// inconsistent with the buffer, or if the status byte is not a known value.
std::optional<StatusUpdate> decode_status(const std::uint8_t* data, std::size_t size);

inline std::optional<StatusUpdate> decode_status(const std::vector<std::uint8_t>& buf) {
  return decode_status(buf.data(), buf.size());
}

}  // namespace clusterrun
