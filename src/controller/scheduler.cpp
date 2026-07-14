#include "controller/scheduler.hpp"

#include <algorithm>

#include "controller/match.hpp"

namespace clusterrun {

void Scheduler::push_pending(JobId id) {
  auto it = jobs_.find(id);
  if (it == jobs_.end()) {
    return;
  }
  const Job& job = it->second.job;
  Band& band = pending_[static_cast<std::size_t>(job.priority)];
  std::deque<JobId>& q = band.by_tenant[job.tenant];
  if (q.empty()) {
    band.rotation.push_back(job.tenant);
  }
  q.push_back(id);
  ++band.size;
}

std::optional<JobId> Scheduler::pop_fair(Band& band) {
  if (band.rotation.empty()) {
    return std::nullopt;
  }
  std::string tenant = band.rotation.front();
  band.rotation.pop_front();
  std::deque<JobId>& q = band.by_tenant[tenant];
  JobId id = q.front();
  q.pop_front();
  --band.size;
  if (q.empty()) {
    band.by_tenant.erase(tenant);
  } else {
    band.rotation.push_back(tenant);  // next turn goes to the other tenants
  }
  return id;
}

void Scheduler::upsert_worker(const Capability& cap) {
  workers_[cap.worker_id] = cap;
  // A newly arrived or refreshed worker may satisfy capability-blocked jobs.
  if (!pending_capability_.empty()) {
    std::vector<JobId> blocked;
    blocked.swap(pending_capability_);
    for (JobId id : blocked) {
      push_pending(id);
    }
  }
}

void Scheduler::remove_worker(WorkerId id) {
  workers_.erase(id);
  // Any job this worker held is no longer being executed; requeue it.
  for (auto& [jid, st] : jobs_) {
    if (st.assigned_to == id && st.last_status != Status::kSucceeded) {
      st.assigned_to.reset();
      st.last_detail = "lost: worker went silent";
      push_pending(jid);
    }
  }
}

void Scheduler::submit(const Job& job) {
  JobState st;
  st.job = job;
  jobs_[job.job_id] = st;
  push_pending(job.job_id);
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
  // High priority drains completely before normal, and normal before low.
  for (int band = 2; band >= 0; --band) {
    while (auto id = pop_fair(pending_[static_cast<std::size_t>(band)])) {
      auto it = jobs_.find(*id);
      if (it == jobs_.end()) {
        continue;
      }
      JobState& st = it->second;
      if (st.assigned_to.has_value()) {
        continue;  // already running somewhere
      }
      auto worker = pick_worker(st.job.reqs);
      if (!worker.has_value()) {
        pending_capability_.push_back(*id);
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
    case Status::kSucceeded: {
      st.assigned_to.reset();
      ++done_;
      // Success detail of the form "result: <uri> sha256:<hex>" records the
      // uploaded artifact so clients can fetch and verify it.
      const std::string kResult = "result: ";
      const std::string kSha = " sha256:";
      if (u.detail.rfind(kResult, 0) == 0) {
        std::size_t sha = u.detail.rfind(kSha);
        if (sha != std::string::npos && sha > kResult.size()) {
          st.result_uri = u.detail.substr(kResult.size(), sha - kResult.size());
          st.result_sha256 = u.detail.substr(sha + kSha.size());
        }
      }
      return JobOutcome::kDone;
    }
    case Status::kFailed:
    case Status::kLost: {
      st.assigned_to.reset();
      st.last_detail = u.detail.empty() ? "lost: worker went silent" : u.detail;
      if (st.attempts < st.job.attempts_max) {
        push_pending(u.job_id);
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

std::size_t Scheduler::pending_count() const {
  return pending_[0].size + pending_[1].size + pending_[2].size;
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
