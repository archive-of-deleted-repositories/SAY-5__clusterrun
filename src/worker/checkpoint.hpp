#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "transport/transport.hpp"

namespace clusterrun {

// A checkpoint captures enough state to resume a job on a different worker. The
// step is the unit of progress the job has completed; state is opaque job data;
// ts_us is when the checkpoint was taken.
struct Checkpoint {
  std::uint64_t step = 0;
  std::string state;
  std::uint64_t ts_us = 0;
};

// Serializes a checkpoint to a compact JSON blob suitable for S3.
std::string encode_checkpoint(const Checkpoint& c);

// Parses a checkpoint blob. Returns nullopt on malformed input.
std::optional<Checkpoint> decode_checkpoint(const std::string& blob);

// The S3 key under which a job's latest checkpoint is stored, within a bucket.
// One key per job: each checkpoint overwrites the previous so resume always
// reads the most recent state.
std::string checkpoint_key(std::uint64_t job_id);

// Writes a checkpoint to bucket/checkpoint_key(job_id) and returns the s3:// uri
// of the object, or empty on failure.
std::string write_checkpoint(ObjectStore& store, const std::string& bucket, std::uint64_t job_id,
                             const Checkpoint& c);

// Reads the checkpoint named by an s3:// uri, or nullopt if absent or malformed.
std::optional<Checkpoint> read_checkpoint(ObjectStore& store, const std::string& s3_uri);

}  // namespace clusterrun
