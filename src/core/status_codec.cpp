#include "core/status_codec.hpp"

#include <cstring>

namespace clusterrun {

namespace {

void put_u16(std::vector<std::uint8_t>* out, std::uint16_t v) {
  out->push_back(static_cast<std::uint8_t>(v & 0xff));
  out->push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
}

void put_u32(std::vector<std::uint8_t>* out, std::uint32_t v) {
  for (int i = 0; i < 4; ++i) {
    out->push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xff));
  }
}

void put_u64(std::vector<std::uint8_t>* out, std::uint64_t v) {
  for (int i = 0; i < 8; ++i) {
    out->push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xff));
  }
}

std::uint16_t get_u16(const std::uint8_t* p) {
  return static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
}

std::uint32_t get_u32(const std::uint8_t* p) {
  std::uint32_t v = 0;
  for (int i = 0; i < 4; ++i) {
    v |= static_cast<std::uint32_t>(p[i]) << (8 * i);
  }
  return v;
}

std::uint64_t get_u64(const std::uint8_t* p) {
  std::uint64_t v = 0;
  for (int i = 0; i < 8; ++i) {
    v |= static_cast<std::uint64_t>(p[i]) << (8 * i);
  }
  return v;
}

bool valid_status(std::uint8_t b) {
  return b <= static_cast<std::uint8_t>(Status::kLost);
}

}  // namespace

std::vector<std::uint8_t> encode_status(const StatusUpdate& u) {
  std::vector<std::uint8_t> out;
  const std::uint32_t len = static_cast<std::uint32_t>(kStatusFixedAfterLen + u.detail.size());
  put_u32(&out, len);
  put_u64(&out, u.job_id);
  out.push_back(static_cast<std::uint8_t>(u.status));
  put_u64(&out, u.ts_us);
  put_u32(&out, u.progress);
  put_u16(&out, u.worker_id);
  out.insert(out.end(), u.detail.begin(), u.detail.end());
  return out;
}

std::optional<StatusUpdate> decode_status(const std::uint8_t* data, std::size_t size) {
  if (data == nullptr || size < 4) {
    return std::nullopt;
  }
  const std::uint32_t len = get_u32(data);
  if (len < kStatusFixedAfterLen) {
    return std::nullopt;
  }
  // The buffer must hold the len prefix plus exactly len more bytes.
  if (size < static_cast<std::size_t>(4) + len) {
    return std::nullopt;
  }

  const std::uint8_t* p = data + 4;
  StatusUpdate u;
  u.job_id = get_u64(p);
  p += 8;
  const std::uint8_t status_byte = *p;
  if (!valid_status(status_byte)) {
    return std::nullopt;
  }
  u.status = static_cast<Status>(status_byte);
  p += 1;
  u.ts_us = get_u64(p);
  p += 8;
  u.progress = get_u32(p);
  p += 4;
  u.worker_id = get_u16(p);
  p += 2;

  const std::size_t detail_len = len - kStatusFixedAfterLen;
  u.detail.assign(reinterpret_cast<const char*>(p), detail_len);
  return u;
}

}  // namespace clusterrun
