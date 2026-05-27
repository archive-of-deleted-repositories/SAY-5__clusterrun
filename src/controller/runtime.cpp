#include "controller/runtime.hpp"

#include "core/base64.hpp"
#include "core/job_codec.hpp"
#include "core/status_codec.hpp"

namespace clusterrun {

namespace {

std::string claim_queue_name(const QueueNames& names, WorkerId id) {
  return names.claims_prefix + std::to_string(id);
}

}  // namespace

bool controller_step(Controller& ctrl, Queue& queue, const QueueNames& names,
                     std::uint64_t now_us) {
  bool worked = false;

  // 1. Heartbeats: refresh liveness and capability.
  std::string hb_url = queue.ensure_queue(names.heartbeat);
  for (auto& m : queue.receive(hb_url, 10, 0)) {
    if (auto cap = decode_capability(m.body)) {
      ctrl.on_heartbeat(*cap, now_us);
      worked = true;
    }
    queue.remove(hb_url, m.receipt);
  }

  // 2. Status updates: fold terminal outcomes into job state, dead-letter as
  // needed.
  std::string status_url = queue.ensure_queue(names.status);
  for (auto& m : queue.receive(status_url, 10, 0)) {
    auto raw = base64_decode(m.body);
    if (raw) {
      if (auto u = decode_status(*raw)) {
        JobOutcome outcome = ctrl.on_status(*u);
        if (outcome == JobOutcome::kDeadLettered) {
          if (const JobState* st = ctrl.scheduler().job_state(u->job_id)) {
            std::string dlq_url = queue.ensure_queue(names.dlq);
            queue.send(dlq_url, encode_job(st->job));
          }
        }
        worked = true;
      }
    }
    queue.remove(status_url, m.receipt);
  }

  // 3. Intake: pull newly submitted job descriptors onto the pending set.
  std::string jobs_url = queue.ensure_queue(names.jobs);
  for (auto& m : queue.receive(jobs_url, 10, 0)) {
    if (auto job = decode_job(m.body)) {
      ctrl.submit(*job);
      worked = true;
    }
    queue.remove(jobs_url, m.receipt);
  }

  // 4. Loss detection: reap silent workers so their jobs are redriven.
  if (!ctrl.tick(now_us).empty()) {
    worked = true;
  }

  // 5. Dispatch: push new claims to per-worker claim queues.
  for (auto& [worker_id, job] : ctrl.assign()) {
    std::string claim_url = queue.ensure_queue(claim_queue_name(names, worker_id));
    queue.send(claim_url, encode_job(job));
    worked = true;
  }

  return worked;
}

}  // namespace clusterrun
