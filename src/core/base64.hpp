#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace clusterrun {

// Standard base64 with padding. Used to carry binary status frames inside the
// text bodies SQS-shaped queues expect.
std::string base64_encode(const std::vector<std::uint8_t>& data);

// Returns nullopt on malformed input (bad characters or length).
std::optional<std::vector<std::uint8_t>> base64_decode(const std::string& text);

}  // namespace clusterrun
