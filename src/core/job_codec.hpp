#pragma once

#include <optional>
#include <string>

#include "core/types.hpp"

namespace clusterrun {

// Serializes a Job to its canonical JSON representation.
std::string encode_job(const Job& job);

// Parses a Job from JSON. Returns nullopt on any malformed or out-of-range
// input rather than throwing, so the parser is safe to fuzz and to feed
// untrusted queue messages. A job is rejected if job_id is absent or command is
// empty; every other field has a documented default.
std::optional<Job> decode_job(const std::string& json);

// Serializes a Capability heartbeat to JSON.
std::string encode_capability(const Capability& cap);

// Parses a Capability heartbeat from JSON. Returns nullopt on malformed input.
std::optional<Capability> decode_capability(const std::string& json);

}  // namespace clusterrun
