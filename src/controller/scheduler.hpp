#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/types.hpp"

namespace clusterrun {

// Outcome of feeding a status update back into the scheduler.
enum class JobOutcome {
  kInProgress,    // claimed/running/checkpointed: still live
  kDone,          // succeeded: terminal success
  kRedriven,      // failed or lost with attempts remaining: requeued
  kDeadLettered,  // failed or lost with no attempts left: sent to DLQ
};

// Per-job bookkeeping the controller keeps in memory.
struct JobState {
  Job job;
  std::uint32_t attempts = 0;           // how many times it has been claimed
  std::optional<WorkerId> assigned_to;  // worker currently holding it, if any
  Status last_status = Status::kClaimed;
  std::string latest_checkpoint_s3;  // most recent checkpoint key, if any
};

// The scheduling core. Pure in-memory logic with no transport dependency so it
// can be unit and property tested directly. The controller loop drives it with
// queue and heartbeat events.
//
// Scheduling policy: a pending job is assigned to the eligible worker with the
// fewest outstanding claims; ties break by ascending worker id (round-robin
// fairness). A job whose requirements no eligible worker can satisfy stays in
// pending_capability until a matching worker registers.
class Scheduler {
 public:
  // Registers or refreshes a worker's advertised capability.
  void upsert_worker(const Capability& cap);

  // Forgets a worker (for example after it is declared lost). Any job it held is
  // redriven on the next assign pass.
  void remove_worker(WorkerId id);

  // Adds a fresh job to the pending set.
  void submit(const Job& job);

  // Assigns as many pending jobs as possible to eligible workers and returns the
  // (worker_id, job) claims produced this pass, in assignment order. Jobs with
  // no eligible worker are moved to pending_capability.
  std::vector<std::pair<WorkerId, Job>> assign();

  // Folds a status update from a worker into job state and returns the outcome.
  JobOutcome on_status(const StatusUpdate& u);

  // Job ids currently waiting because no worker matches their requirements,
  // paired with a human-readable reason.
  std::map<JobId, std::string> waiting_for() const;

  std::size_t pending_count() const {
    return pending_.size();
  }
  std::size_t in_flight_count() const;
  std::size_t done_count() const {
    return done_;
  }
  std::size_t dlq_count() const {
    return dlq_;
  }
  const std::vector<JobId>& dead_letters() const {
    return dead_letters_;
  }

  // Looks up the live state of a job, or nullptr if unknown.
  const JobState* job_state(JobId id) const;

 private:
  // Counts how many jobs a worker currently holds.
  std::size_t outstanding_for(WorkerId id) const;
  // Picks the eligible worker with the fewest claims, ties by worker id.
  std::optional<WorkerId> pick_worker(const Requirements& reqs) const;
  // Builds the unmet-requirement reason string for a pending-capability job.
  std::string describe_gap(const Requirements& reqs) const;

  std::map<WorkerId, Capability> workers_;
  std::map<JobId, JobState> jobs_;
  std::deque<JobId> pending_;              // ready to assign
  std::vector<JobId> pending_capability_;  // no matching worker yet
  std::size_t done_ = 0;
  std::size_t dlq_ = 0;
  std::vector<JobId> dead_letters_;
};

}  // namespace clusterrun
