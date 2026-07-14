#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace clusterrun {

using JobId = std::uint64_t;
using WorkerId = std::uint16_t;

// Hardware and placement requirements a job declares. A worker is eligible only
// if it satisfies every field: enough cpu and memory, all requested features,
// and a matching node_class (empty node_class means any class is acceptable).
struct Requirements {
  std::uint32_t cpu_min = 0;
  std::uint32_t mem_gb_min = 0;
  std::vector<std::string> features;  // e.g. "avx2", "gpu"
  std::string node_class;             // "aws", "local", "gpu", "cpu-heavy", or ""
};

// Dispatch priority for a job. Within one priority band jobs are scheduled
// fairly across tenants; a higher band always dispatches before a lower one.
enum class Priority : std::uint8_t {
  kLow = 0,
  kNormal = 1,
  kHigh = 2,
};

// Canonical lowercase token for a priority ("low", "normal", "high").
const char* to_string(Priority p);

// Parses a priority token. Returns false for anything else.
bool parse_priority(const std::string& token, Priority* out);

// A unit of work. command[0] is the executable; the remainder are arguments.
struct Job {
  JobId job_id = 0;
  std::vector<std::string> command;
  std::string stdout_s3;
  std::string stderr_s3;
  Requirements reqs;  // JSON key is "requires"; field renamed to avoid the C++20 keyword.
  std::uint32_t attempts_max = 1;
  std::uint32_t checkpoint_interval_s = 0;  // 0 disables checkpointing
  std::string idempotency_key;
  Priority priority = Priority::kNormal;
  std::string tenant;  // fair-share bucket; empty means the default tenant
  // Set by the controller on redrive so a resuming worker can pick up state.
  std::string resume_from_checkpoint_s3;
};

// What a worker advertises about itself in its heartbeat.
struct Capability {
  WorkerId worker_id = 0;
  std::uint32_t cpu = 0;
  std::uint32_t mem_gb = 0;
  std::vector<std::string> features;
  std::string node_class;
};

enum class Status : std::uint8_t {
  kClaimed = 0,
  kRunning = 1,
  kCheckpointed = 2,
  kSucceeded = 3,
  kFailed = 4,
  kLost = 5,
};

// A status update streamed from a worker to the controller.
struct StatusUpdate {
  JobId job_id = 0;
  Status status = Status::kClaimed;
  std::uint64_t ts_us = 0;
  std::uint32_t progress = 0;  // 0..100
  WorkerId worker_id = 0;
  std::string detail;  // free-form: checkpoint key, error message, etc.
};

const char* to_string(Status s);

}  // namespace clusterrun
