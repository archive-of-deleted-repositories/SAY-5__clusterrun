#pragma once

#include <cstdint>
#include <map>
#include <vector>

#include "core/types.hpp"

namespace clusterrun {

// Tracks the last time each worker was heard from and reports which workers have
// gone silent past a timeout. Time is supplied by the caller (monotonic
// microseconds) so the logic is deterministic and testable without sleeping.
class HeartbeatTracker {
 public:
  // timeout_us is how long a worker may be silent before being declared lost.
  // The default of 30s matches the controller's loss policy.
  explicit HeartbeatTracker(std::uint64_t timeout_us = 30ull * 1000 * 1000)
      : timeout_us_(timeout_us) {
  }

  // Records that worker_id was heard from at now_us.
  void beat(WorkerId worker_id, std::uint64_t now_us);

  // Returns workers last heard from more than timeout_us ago and forgets them so
  // each loss is reported once.
  std::vector<WorkerId> reap_lost(std::uint64_t now_us);

  // Whether a worker is currently tracked as alive.
  bool alive(WorkerId worker_id) const;

  std::size_t tracked_count() const {
    return last_seen_.size();
  }

 private:
  std::uint64_t timeout_us_;
  std::map<WorkerId, std::uint64_t> last_seen_;
};

}  // namespace clusterrun
