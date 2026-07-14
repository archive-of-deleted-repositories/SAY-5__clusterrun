#pragma once

#include <string>

namespace clusterrun {

// Computes the SHA-256 digest of data and returns it as 64 lowercase hex
// characters. Used to checksum result artifacts so a client can verify what
// it downloads is what the worker uploaded.
std::string sha256_hex(const std::string& data);

}  // namespace clusterrun
