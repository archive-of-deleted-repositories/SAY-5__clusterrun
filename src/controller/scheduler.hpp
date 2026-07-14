#pragma once

#include <array>
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
  std::string last_detail;           // detail of the most recent failure, if any
};

// The scheduling core. Pure in-memory logic with no transport dependency so it
// can be unit and property tested directly. The controller loop drives it with
// queue and heartbeat events.
//
// Scheduling policy: pending jobs dispatch strictly by priority band (high
// before normal before low). Within a band, tenants take turns in round-robin
// order so one tenant's burst cannot starve another. The chosen job goes to
// the eligible worker with the fewest outstanding claims; ties break by
// ascending worker id. A job whose requirements no eligible worker can satisfy
// stays in pending_capability until a matching worker registers.
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

  std::size_t pending_count() const;
  // Pending jobs queued in one priority band.
  std::size_t pending_count(Priority p) const {
    return pending_[static_cast<std::size_t>(p)].size;
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
  // One priority band: a queue per tenant plus the round-robin turn order.
  // Invariant: a tenant is in rotation exactly once iff its queue is non-empty.
  struct Band {
    std::map<std::string, std::deque<JobId>> by_tenant;
    std::deque<std::string> rotation;
    std::size_t size = 0;
  };

  // Queues a job into its priority band and tenant queue.
  void push_pending(JobId id);
  // Pops the next job of a band in fair rotation order; nullopt when empty.
  static std::optional<JobId> pop_fair(Band& band);

  // Counts how many jobs a worker currently holds.
  std::size_t outstanding_for(WorkerId id) const;
  // Picks the eligible worker with the fewest claims, ties by worker id.
  std::optional<WorkerId> pick_worker(const Requirements& reqs) const;
  // Builds the unmet-requirement reason string for a pending-capability job.
  std::string describe_gap(const Requirements& reqs) const;

  std::map<WorkerId, Capability> workers_;
  std::map<JobId, JobState> jobs_;
  std::array<Band, 3> pending_;            // ready to assign, indexed by Priority
  std::vector<JobId> pending_capability_;  // no matching worker yet
  std::size_t done_ = 0;
  std::size_t dlq_ = 0;
  std::vector<JobId> dead_letters_;
};

}  // namespace clusterrun
