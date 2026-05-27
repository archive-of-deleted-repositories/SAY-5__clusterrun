#pragma once

#include <functional>
#include <string>
#include <vector>

#include "core/types.hpp"
#include "transport/transport.hpp"

namespace clusterrun {

// Sink the worker calls for each status update it produces. The controller-side
// loop wires this to the status queue; tests can capture updates directly.
using StatusSink = std::function<void(const StatusUpdate&)>;

// Endpoints and identity a worker needs to operate.
struct WorkerConfig {
  Capability capability;
  std::string job_queue_url;
  std::string status_queue_url;     // where binary status frames are sent
  std::string heartbeat_queue_url;  // where capability heartbeats are sent
};

// Executes a single job: emits kClaimed, runs the command, streams stdout and
// stderr to their S3 destinations, then emits kSucceeded or kFailed. Returns the
// terminal status. The status sink receives every intermediate update so the
// controller observes the lifecycle. extra_env is passed to the child process
// (used to hand a resume checkpoint key to a resuming worker).
Status execute_job(const Job& job, WorkerId worker_id, ObjectStore& store, const StatusSink& sink,
                   const std::vector<std::string>& extra_env = {});

// Builds a capability heartbeat JSON payload for this worker.
std::string make_heartbeat(const Capability& cap);

// One pass of the worker loop driven by a queue and object store: sends a
// heartbeat, then claims and runs at most one job from its claim queue, sending
// each status update as a base64 binary frame to the status queue. Returns true
// if a job was run this pass. Defined in runtime.cpp.
bool worker_step(const WorkerConfig& cfg, Queue& queue, ObjectStore& store,
                 const std::string& claim_queue_url);

}  // namespace clusterrun
