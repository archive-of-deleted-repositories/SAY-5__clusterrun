#include "controller/scheduler.hpp"

#include <algorithm>

#include "controller/match.hpp"

namespace clusterrun {

void Scheduler::upsert_worker(const Capability& cap) {
  workers_[cap.worker_id] = cap;
  // A newly arrived or refreshed worker may satisfy capability-blocked jobs.
  if (!pending_capability_.empty()) {
    for (JobId id : pending_capability_) {
      pending_.push_back(id);
    }
    pending_capability_.clear();
  }
}

void Scheduler::remove_worker(WorkerId id) {
  workers_.erase(id);
  // Any job this worker held is no longer being executed; requeue it.
  for (auto& [jid, st] : jobs_) {
    if (st.assigned_to == id && st.last_status != Status::kSucceeded) {
      st.assigned_to.reset();
      pending_.push_back(jid);
    }
  }
}

void Scheduler::submit(const Job& job) {
  JobState st;
  st.job = job;
  jobs_[job.job_id] = st;
  pending_.push_back(job.job_id);
}

std::size_t Scheduler::outstanding_for(WorkerId id) const {
  std::size_t n = 0;
  for (const auto& [jid, st] : jobs_) {
    if (st.assigned_to == id) {
      ++n;
    }
  }
  return n;
}

std::optional<WorkerId> Scheduler::pick_worker(const Requirements& reqs) const {
  std::optional<WorkerId> best;
  std::size_t best_load = 0;
  for (const auto& [id, cap] : workers_) {  // ascending worker id
    if (!capability_matches(reqs, cap)) {
      continue;
    }
    std::size_t load = outstanding_for(id);
    if (!best.has_value() || load < best_load) {
      best = id;
      best_load = load;
    }
  }
  return best;
}

std::string Scheduler::describe_gap(const Requirements& reqs) const {
  std::string r = "no worker satisfies";
  if (reqs.cpu_min > 0) {
    r += " cpu>=" + std::to_string(reqs.cpu_min);
  }
  if (reqs.mem_gb_min > 0) {
    r += " mem>=" + std::to_string(reqs.mem_gb_min) + "g";
  }
  for (const auto& f : reqs.features) {
    r += " feature:" + f;
  }
  if (!reqs.node_class.empty()) {
    r += " node_class:" + reqs.node_class;
  }
  return r;
}

std::vector<std::pair<WorkerId, Job>> Scheduler::assign() {
  std::vector<std::pair<WorkerId, Job>> claims;
  while (!pending_.empty()) {
    JobId id = pending_.front();
    pending_.pop_front();
    auto it = jobs_.find(id);
    if (it == jobs_.end()) {
      continue;
    }
    JobState& st = it->second;
    if (st.assigned_to.has_value()) {
      continue;  // already running somewhere
    }
    auto worker = pick_worker(st.job.reqs);
    if (!worker.has_value()) {
      pending_capability_.push_back(id);
      continue;
    }
    st.assigned_to = worker;
    st.attempts += 1;
    st.last_status = Status::kClaimed;
    // Carry the latest checkpoint forward so a resuming worker can use it.
    Job dispatched = st.job;
    if (!st.latest_checkpoint_s3.empty()) {
      dispatched.resume_from_checkpoint_s3 = st.latest_checkpoint_s3;
    }
    claims.emplace_back(*worker, dispatched);
  }
  return claims;
}

JobOutcome Scheduler::on_status(const StatusUpdate& u) {
  auto it = jobs_.find(u.job_id);
  if (it == jobs_.end()) {
    return JobOutcome::kInProgress;
  }
  JobState& st = it->second;
  st.last_status = u.status;

  switch (u.status) {
    case Status::kClaimed:
    case Status::kRunning:
      return JobOutcome::kInProgress;
    case Status::kCheckpointed:
      if (!u.detail.empty()) {
        st.latest_checkpoint_s3 = u.detail;
      }
      return JobOutcome::kInProgress;
    case Status::kSucceeded:
      st.assigned_to.reset();
      ++done_;
      return JobOutcome::kDone;
    case Status::kFailed:
    case Status::kLost: {
      st.assigned_to.reset();
      if (st.attempts < st.job.attempts_max) {
        pending_.push_back(u.job_id);
        return JobOutcome::kRedriven;
      }
      ++dlq_;
      dead_letters_.push_back(u.job_id);
      return JobOutcome::kDeadLettered;
    }
  }
  return JobOutcome::kInProgress;
}

std::map<JobId, std::string> Scheduler::waiting_for() const {
  std::map<JobId, std::string> out;
  for (JobId id : pending_capability_) {
    auto it = jobs_.find(id);
    if (it != jobs_.end()) {
      out[id] = describe_gap(it->second.job.reqs);
    }
  }
  return out;
}

std::size_t Scheduler::in_flight_count() const {
  std::size_t n = 0;
  for (const auto& [id, st] : jobs_) {
    if (st.assigned_to.has_value()) {
      ++n;
    }
  }
  return n;
}

const JobState* Scheduler::job_state(JobId id) const {
  auto it = jobs_.find(id);
  return it == jobs_.end() ? nullptr : &it->second;
}

}  // namespace clusterrun
