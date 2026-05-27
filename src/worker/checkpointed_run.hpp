#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "core/types.hpp"
#include "transport/transport.hpp"
#include "worker/checkpoint.hpp"
#include "worker/worker.hpp"

namespace clusterrun {

// Models a heavy-compute job as a sequence of equal steps. Each step advances
// state; the worker checkpoints every checkpoint_interval steps and can resume
// from a checkpoint produced by an earlier worker. This is the in-process
// counterpart to a cooperating external command that reads CLUSTERRUN_RESUME_FROM
// and writes its progress, modeled here so the resume path is deterministically
// testable.
struct StepJob {
  std::uint64_t total_steps = 0;
  // Applies one step to the running state and returns the new state. Pure so a
  // resume reproduces the same trajectory.
  std::function<std::string(std::uint64_t step, const std::string& state)> apply_step;
};

// Result of a (possibly partial) checkpointed run.
struct CheckpointedResult {
  bool completed = false;          // reached total_steps
  std::uint64_t steps_done = 0;    // steps executed this run (excludes resumed work)
  std::uint64_t resumed_from = 0;  // step the run resumed at (0 if fresh)
  std::string final_state;         // state after the last executed step
  std::string last_checkpoint_s3;  // uri of the most recent checkpoint written
};

// Runs a StepJob with periodic checkpointing. If job.resume_from_checkpoint_s3 is
// set, the run resumes from that checkpoint's step and state. stop_after_steps
// caps how many steps run this pass (used by tests to simulate a worker killed
// mid-run); 0 means run to completion. Each checkpoint is written to bucket and
// emitted as a kCheckpointed status whose detail is the checkpoint uri.
CheckpointedResult run_with_checkpoints(const Job& job, const StepJob& step_job, WorkerId worker_id,
                                        ObjectStore& store, const std::string& bucket,
                                        std::uint64_t checkpoint_interval_steps,
                                        std::uint64_t stop_after_steps, const StatusSink& sink);

}  // namespace clusterrun
