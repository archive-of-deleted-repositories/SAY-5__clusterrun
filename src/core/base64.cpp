#include "core/base64.hpp"

namespace clusterrun {

namespace {

constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int decode_char(char c) {
  if (c >= 'A' && c <= 'Z') {
    return c - 'A';
  }
  if (c >= 'a' && c <= 'z') {
    return c - 'a' + 26;
  }
  if (c >= '0' && c <= '9') {
    return c - '0' + 52;
  }
  if (c == '+') {
    return 62;
  }
  if (c == '/') {
    return 63;
  }
  return -1;
}

}  // namespace

std::string base64_encode(const std::vector<std::uint8_t>& data) {
  std::string out;
  out.reserve((data.size() + 2) / 3 * 4);
  std::size_t i = 0;
  while (i + 3 <= data.size()) {
    std::uint32_t n = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
    out.push_back(kAlphabet[(n >> 18) & 0x3f]);
    out.push_back(kAlphabet[(n >> 12) & 0x3f]);
    out.push_back(kAlphabet[(n >> 6) & 0x3f]);
    out.push_back(kAlphabet[n & 0x3f]);
    i += 3;
  }
  std::size_t rem = data.size() - i;
  if (rem == 1) {
    std::uint32_t n = data[i] << 16;
    out.push_back(kAlphabet[(n >> 18) & 0x3f]);
    out.push_back(kAlphabet[(n >> 12) & 0x3f]);
    out.push_back('=');
    out.push_back('=');
  } else if (rem == 2) {
    std::uint32_t n = (data[i] << 16) | (data[i + 1] << 8);
    out.push_back(kAlphabet[(n >> 18) & 0x3f]);
    out.push_back(kAlphabet[(n >> 12) & 0x3f]);
    out.push_back(kAlphabet[(n >> 6) & 0x3f]);
    out.push_back('=');
  }
  return out;
}

std::optional<std::vector<std::uint8_t>> base64_decode(const std::string& text) {
  if (text.size() % 4 != 0) {
    return std::nullopt;
  }
  std::vector<std::uint8_t> out;
  out.reserve(text.size() / 4 * 3);
  for (std::size_t i = 0; i < text.size(); i += 4) {
    int pad = 0;
    int vals[4];
    for (int k = 0; k < 4; ++k) {
      char c = text[i + k];
      if (c == '=') {
        // Padding may only appear in the last group, last one or two chars.
        if (i + 4 != text.size() || k < 2) {
          return std::nullopt;
        }
        vals[k] = 0;
        ++pad;
      } else {
        int d = decode_char(c);
        if (d < 0 || pad > 0) {
          return std::nullopt;  // non-base64 char, or data after padding
        }
        vals[k] = d;
      }
    }
    std::uint32_t n = (vals[0] << 18) | (vals[1] << 12) | (vals[2] << 6) | vals[3];
    out.push_back(static_cast<std::uint8_t>((n >> 16) & 0xff));
    if (pad < 2) {
      out.push_back(static_cast<std::uint8_t>((n >> 8) & 0xff));
    }
    if (pad < 1) {
      out.push_back(static_cast<std::uint8_t>(n & 0xff));
    }
  }
  return out;
}

}  // namespace clusterrun
