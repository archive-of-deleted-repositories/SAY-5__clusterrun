#include "controller/scheduler.hpp"

#include <algorithm>

#include "controller/match.hpp"
#include "controller/retry_classifier.hpp"

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

bool Scheduler::drain_worker(WorkerId id) {
  if (workers_.count(id) == 0) {
    return false;
  }
  draining_.insert(id);
  return true;
}

void Scheduler::remove_worker(WorkerId id) {
  workers_.erase(id);
  draining_.erase(id);
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
  ++submitted_;
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
    if (draining_.count(id) > 0) {
      continue;  // finishing what it has, takes nothing new
    }
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
      if (st.claimed_ts_us == 0 && u.ts_us > 0) {
        st.claimed_ts_us = u.ts_us;
        if (first_claim_ts_us_ == 0 || u.ts_us < first_claim_ts_us_) {
          first_claim_ts_us_ = u.ts_us;
        }
      }
      return JobOutcome::kInProgress;
    case Status::kCheckpointed:
      if (!u.detail.empty()) {
        st.latest_checkpoint_s3 = u.detail;
      }
      return JobOutcome::kInProgress;
    case Status::kSucceeded: {
      st.assigned_to.reset();
      ++done_;
      if (st.claimed_ts_us > 0 && u.ts_us > st.claimed_ts_us) {
        latencies_us_.push_back(u.ts_us - st.claimed_ts_us);
      }
      if (u.ts_us > last_done_ts_us_) {
        last_done_ts_us_ = u.ts_us;
      }
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
      st.claimed_ts_us = 0;  // the next claim starts a fresh latency window
      if (st.attempts < st.job.attempts_max) {
        retries_by_category_[to_string(parse_category(st.last_detail))] += 1;
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

SchedulerStats Scheduler::stats() const {
  SchedulerStats s;
  s.submitted = submitted_;
  s.done = done_;
  s.dead_lettered = dlq_;
  s.retries_by_category = retries_by_category_;
  if (!latencies_us_.empty()) {
    std::vector<std::uint64_t> sorted = latencies_us_;
    std::sort(sorted.begin(), sorted.end());
    // Nearest-rank percentiles, rounding up so p95 of a small sample set is
    // pulled toward the slow tail rather than the median.
    s.latency_p50_us = sorted[((sorted.size() - 1) * 50 + 99) / 100];
    s.latency_p95_us = sorted[((sorted.size() - 1) * 95 + 99) / 100];
  }
  if (done_ > 0 && last_done_ts_us_ > first_claim_ts_us_ && first_claim_ts_us_ > 0) {
    double window_s = static_cast<double>(last_done_ts_us_ - first_claim_ts_us_) / 1e6;
    s.throughput_per_s = static_cast<double>(done_) / window_s;
  }
  return s;
}

}  // namespace clusterrun
