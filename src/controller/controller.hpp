#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "controller/heartbeat.hpp"
#include "controller/scheduler.hpp"
#include "core/types.hpp"

namespace clusterrun {

// Ties heartbeat tracking to the scheduler. A heartbeat both refreshes the
// worker's liveness and its advertised capability; a status update is folded
// into job state; a periodic tick reaps silent workers and redrives the jobs
// they were holding.
class Controller {
 public:
  explicit Controller(std::uint64_t heartbeat_timeout_us = 30ull * 1000 * 1000)
      : hb_(heartbeat_timeout_us) {
  }

  void submit(const Job& job) {
    sched_.submit(job);
  }

  // Records a capability heartbeat at now_us: refreshes liveness and capability.
  void on_heartbeat(const Capability& cap, std::uint64_t now_us);

  // Folds a worker status update into scheduler state.
  JobOutcome on_status(const StatusUpdate& u) {
    return sched_.on_status(u);
  }

  // Produces the next batch of (worker, job) claims.
  std::vector<std::pair<WorkerId, Job>> assign() {
    return sched_.assign();
  }

  // Jobs blocked because no worker matches their requirements (for example a gpu
  // job with no gpu worker), each with a human-readable waiting reason.
  std::map<JobId, std::string> waiting_for() const {
    return sched_.waiting_for();
  }

  // Reaps workers silent past the timeout, removes them from scheduling, and
  // returns the ids declared lost (their jobs are redriven on the next assign).
  std::vector<WorkerId> tick(std::uint64_t now_us);

  Scheduler& scheduler() {
    return sched_;
  }
  const Scheduler& scheduler() const {
    return sched_;
  }
  HeartbeatTracker& heartbeats() {
    return hb_;
  }

 private:
  Scheduler sched_;
  HeartbeatTracker hb_;
};

}  // namespace clusterrun
