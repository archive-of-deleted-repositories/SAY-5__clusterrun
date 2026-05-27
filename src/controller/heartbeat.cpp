#include "controller/heartbeat.hpp"

namespace clusterrun {

void HeartbeatTracker::beat(WorkerId worker_id, std::uint64_t now_us) {
  last_seen_[worker_id] = now_us;
}

std::vector<WorkerId> HeartbeatTracker::reap_lost(std::uint64_t now_us) {
  std::vector<WorkerId> lost;
  for (auto it = last_seen_.begin(); it != last_seen_.end();) {
    // Guard against clock going backwards: only reap when now is strictly later.
    if (now_us > it->second && now_us - it->second > timeout_us_) {
      lost.push_back(it->first);
      it = last_seen_.erase(it);
    } else {
      ++it;
    }
  }
  return lost;
}

bool HeartbeatTracker::alive(WorkerId worker_id) const {
  return last_seen_.find(worker_id) != last_seen_.end();
}

}  // namespace clusterrun
