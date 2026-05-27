#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "core/types.hpp"
#include "fakes.hpp"
#include "worker/checkpoint.hpp"
#include "worker/checkpointed_run.hpp"

using namespace clusterrun;

namespace {

// A deterministic step job: state accumulates the step index, so the final state
// after N steps is the same whether run straight through or resumed partway.
StepJob counting_job(std::uint64_t steps) {
  StepJob j;
  j.total_steps = steps;
  j.apply_step = [](std::uint64_t step, const std::string& state) {
    return state + std::to_string(step) + ",";
  };
  return j;
}

Job descriptor(JobId id, std::uint32_t interval_s) {
  Job d;
  d.job_id = id;
  d.command = {"compute"};
  d.attempts_max = 3;
  d.checkpoint_interval_s = interval_s;
  return d;
}

}  // namespace

TEST(Checkpoint, BlobRoundTrips) {
  Checkpoint c;
  c.step = 10;
  c.state = "0,1,2,3,4,5,6,7,8,9,";
  c.ts_us = 123456;
  auto out = decode_checkpoint(encode_checkpoint(c));
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->step, 10u);
  EXPECT_EQ(out->state, c.state);
  EXPECT_EQ(out->ts_us, 123456u);
}

TEST(Checkpoint, RejectsGarbage) {
  EXPECT_FALSE(decode_checkpoint("nonsense").has_value());
  EXPECT_FALSE(decode_checkpoint("{}").has_value());  // missing step
}

TEST(Checkpoint, WriteReadViaStore) {
  FakeObjectStore store;
  Checkpoint c;
  c.step = 5;
  c.state = "abc";
  std::string uri = write_checkpoint(store, "ckpt", 42, c);
  ASSERT_FALSE(uri.empty());
  auto back = read_checkpoint(store, uri);
  ASSERT_TRUE(back.has_value());
  EXPECT_EQ(back->step, 5u);
  EXPECT_EQ(back->state, "abc");
}

// The headline scenario: a 30s job checkpointing every 10s is killed at 15s, and
// a second worker resumes from the 10s checkpoint. We model 1 step per simulated
// second. The second worker must resume at step 10 (not 0) and the total work
// performed (steps before kill + steps after resume) must be well below a full
// double-run, and the final state must match a clean single run.
TEST(Checkpoint, ResumeOnDifferentWorkerAfterMidRunLoss) {
  FakeObjectStore store;
  const std::uint64_t kTotal = 30;     // 30s job
  const std::uint64_t kInterval = 10;  // checkpoint every 10s
  const std::uint64_t kKillAt = 15;    // worker killed at 15s

  StepJob job_a = counting_job(kTotal);

  // Worker 1 runs and is killed after 15 steps. Its last checkpoint is at step 10.
  std::vector<Status> seen_a;
  auto sink_a = [&](const StatusUpdate& u) { seen_a.push_back(u.status); };
  Job d1 = descriptor(1, kInterval);
  CheckpointedResult r1 = run_with_checkpoints(d1, job_a, /*worker=*/1, store, "ckpt", kInterval,
                                               /*stop_after_steps=*/kKillAt, sink_a);
  EXPECT_FALSE(r1.completed);
  EXPECT_EQ(r1.steps_done, kKillAt);
  ASSERT_FALSE(r1.last_checkpoint_s3.empty());
  // The checkpoint visible to a resuming worker is the one at step 10.
  auto ck = read_checkpoint(store, r1.last_checkpoint_s3);
  ASSERT_TRUE(ck.has_value());
  EXPECT_EQ(ck->step, 10u);

  // The controller redrives onto worker 2, passing the latest checkpoint uri.
  Job d2 = descriptor(1, kInterval);
  d2.resume_from_checkpoint_s3 = r1.last_checkpoint_s3;
  StepJob job_b = counting_job(kTotal);
  std::vector<Status> seen_b;
  auto sink_b = [&](const StatusUpdate& u) { seen_b.push_back(u.status); };
  CheckpointedResult r2 = run_with_checkpoints(d2, job_b, /*worker=*/2, store, "ckpt", kInterval,
                                               /*stop_after_steps=*/0, sink_b);

  EXPECT_TRUE(r2.completed);
  EXPECT_EQ(r2.resumed_from, 10u);         // resumed from the 10s checkpoint
  EXPECT_EQ(r2.steps_done, kTotal - 10u);  // only 20 steps of fresh work

  // Total work (simulated wall-clock seconds) = 15 before kill + 20 after resume.
  std::uint64_t total_work = r1.steps_done + r2.steps_done;
  EXPECT_EQ(total_work, 35u);
  EXPECT_LT(total_work, 40u);  // well under the 60s a full restart would cost

  // Resuming reproduces exactly the state a clean single run would reach.
  StepJob clean = counting_job(kTotal);
  Job d_clean = descriptor(2, kInterval);
  std::vector<Status> seen_c;
  auto sink_c = [&](const StatusUpdate& u) { seen_c.push_back(u.status); };
  CheckpointedResult rc =
      run_with_checkpoints(d_clean, clean, 3, store, "ckpt2", kInterval, 0, sink_c);
  EXPECT_EQ(r2.final_state, rc.final_state);
}
