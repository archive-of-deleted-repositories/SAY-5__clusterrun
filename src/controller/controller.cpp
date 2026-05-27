#include "controller/controller.hpp"

namespace clusterrun {

void Controller::on_heartbeat(const Capability& cap, std::uint64_t now_us) {
  hb_.beat(cap.worker_id, now_us);
  sched_.upsert_worker(cap);
}

std::vector<WorkerId> Controller::tick(std::uint64_t now_us) {
  std::vector<WorkerId> lost = hb_.reap_lost(now_us);
  for (WorkerId id : lost) {
    // Drop the worker from scheduling; any job it held is requeued for redrive.
    sched_.remove_worker(id);
  }
  return lost;
}

}  // namespace clusterrun
