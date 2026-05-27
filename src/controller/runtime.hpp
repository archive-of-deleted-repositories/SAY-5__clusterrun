#pragma once

#include <cstdint>
#include <string>

#include "controller/controller.hpp"
#include "transport/transport.hpp"

namespace clusterrun {

// Names of the queues the controller and workers share. Per-worker claim queues
// are this prefix plus the worker id.
struct QueueNames {
  std::string jobs = "clusterrun-jobs";
  std::string status = "clusterrun-status";
  std::string heartbeat = "clusterrun-heartbeat";
  std::string dlq = "clusterrun-dlq";
  std::string claims_prefix = "clusterrun-claims-";  // + worker_id
};

// One pass of the controller event loop, driven by the supplied clock so it is
// testable: drains heartbeats, drains status updates, ticks loss detection,
// then dispatches new claims to per-worker claim queues. Returns true if any
// work (a claim, status, or heartbeat) was processed this pass.
bool controller_step(Controller& ctrl, Queue& queue, const QueueNames& names, std::uint64_t now_us);

}  // namespace clusterrun
