#include "worker/checkpointed_run.hpp"

#include <chrono>

namespace clusterrun {

namespace {

std::uint64_t now_us() {
  using namespace std::chrono;
  return static_cast<std::uint64_t>(
      duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}

StatusUpdate make_update(JobId job_id, WorkerId worker_id, Status s, std::uint32_t progress,
                         std::string detail) {
  StatusUpdate u;
  u.job_id = job_id;
  u.status = s;
  u.ts_us = now_us();
  u.progress = progress;
  u.worker_id = worker_id;
  u.detail = std::move(detail);
  return u;
}

}  // namespace

CheckpointedResult run_with_checkpoints(const Job& job, const StepJob& step_job, WorkerId worker_id,
                                        ObjectStore& store, const std::string& bucket,
                                        std::uint64_t checkpoint_interval_steps,
                                        std::uint64_t stop_after_steps, const StatusSink& sink) {
  CheckpointedResult result;

  // Resume from a prior checkpoint if the controller handed one back.
  std::uint64_t start_step = 0;
  std::string state;
  if (!job.resume_from_checkpoint_s3.empty()) {
    if (auto ck = read_checkpoint(store, job.resume_from_checkpoint_s3)) {
      start_step = ck->step;
      state = ck->state;
      result.resumed_from = ck->step;
    }
  }

  sink(make_update(job.job_id, worker_id, Status::kClaimed, 0, ""));
  sink(make_update(job.job_id, worker_id, Status::kRunning,
                   step_job.total_steps
                       ? static_cast<std::uint32_t>(start_step * 100 / step_job.total_steps)
                       : 0,
                   ""));

  std::uint64_t executed = 0;
  for (std::uint64_t step = start_step; step < step_job.total_steps; ++step) {
    state = step_job.apply_step(step, state);
    ++executed;
    std::uint64_t completed_steps = step + 1;

    bool at_interval =
        checkpoint_interval_steps > 0 && completed_steps % checkpoint_interval_steps == 0;
    if (at_interval) {
      Checkpoint ck;
      ck.step = completed_steps;
      ck.state = state;
      ck.ts_us = now_us();
      std::string uri = write_checkpoint(store, bucket, job.job_id, ck);
      result.last_checkpoint_s3 = uri;
      std::uint32_t progress =
          static_cast<std::uint32_t>(completed_steps * 100 / step_job.total_steps);
      sink(make_update(job.job_id, worker_id, Status::kCheckpointed, progress, uri));
    }

    // Simulate a worker killed mid-run: stop without a success status.
    if (stop_after_steps > 0 && executed >= stop_after_steps) {
      result.steps_done = executed;
      result.final_state = state;
      return result;
    }
  }

  result.completed = true;
  result.steps_done = executed;
  result.final_state = state;
  sink(make_update(job.job_id, worker_id, Status::kSucceeded, 100, state));
  return result;
}

}  // namespace clusterrun
