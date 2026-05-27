#include <gtest/gtest.h>

#include "controller/controller.hpp"
#include "controller/heartbeat.hpp"

using namespace clusterrun;

namespace {

constexpr std::uint64_t kSec = 1000ull * 1000;

Capability worker(WorkerId id) {
  Capability c;
  c.worker_id = id;
  c.cpu = 8;
  c.mem_gb = 16;
  return c;
}

Job job(JobId id, std::uint32_t attempts = 3) {
  Job j;
  j.job_id = id;
  j.command = {"true"};
  j.attempts_max = attempts;
  return j;
}

}  // namespace

TEST(Heartbeat, DetectsLossAfterTimeout) {
  HeartbeatTracker hb(30 * kSec);
  hb.beat(1, 0);
  // Still within the window at 29s.
  EXPECT_TRUE(hb.reap_lost(29 * kSec).empty());
  EXPECT_TRUE(hb.alive(1));
  // Past 30s the worker is reaped.
  auto lost = hb.reap_lost(31 * kSec);
  ASSERT_EQ(lost.size(), 1u);
  EXPECT_EQ(lost[0], 1);
  EXPECT_FALSE(hb.alive(1));
  // Reported only once.
  EXPECT_TRUE(hb.reap_lost(60 * kSec).empty());
}

TEST(Heartbeat, BeatRefreshesWindow) {
  HeartbeatTracker hb(30 * kSec);
  hb.beat(1, 0);
  hb.beat(1, 25 * kSec);  // refresh before the deadline
  EXPECT_TRUE(hb.reap_lost(50 * kSec).empty());
  EXPECT_TRUE(hb.alive(1));
}

TEST(Controller, LostWorkerMidRunRedrivesJob) {
  Controller c(30 * kSec);
  c.on_heartbeat(worker(1), 0);
  c.on_heartbeat(worker(2), 0);
  c.submit(job(100));

  auto claims = c.assign();
  ASSERT_EQ(claims.size(), 1u);
  WorkerId holder = claims[0].first;
  c.on_status([&] {
    StatusUpdate u;
    u.job_id = 100;
    u.status = Status::kRunning;
    u.worker_id = holder;
    return u;
  }());

  // The surviving worker keeps beating; the holder goes silent.
  WorkerId survivor = holder == 1 ? 2 : 1;
  c.on_heartbeat(worker(survivor), 31 * kSec);

  auto lost = c.tick(31 * kSec);
  ASSERT_EQ(lost.size(), 1u);
  EXPECT_EQ(lost[0], holder);

  // The job should be redriven onto the survivor.
  auto reassigned = c.assign();
  ASSERT_EQ(reassigned.size(), 1u);
  EXPECT_EQ(reassigned[0].first, survivor);
  EXPECT_EQ(reassigned[0].second.job_id, 100u);
}

TEST(Controller, ExhaustedAttemptsDeadLetter) {
  Controller c(30 * kSec);
  c.on_heartbeat(worker(1), 0);
  c.submit(job(7, /*attempts=*/2));

  ASSERT_EQ(c.assign().size(), 1u);
  StatusUpdate f;
  f.job_id = 7;
  f.status = Status::kFailed;
  f.worker_id = 1;
  EXPECT_EQ(c.on_status(f), JobOutcome::kRedriven);
  ASSERT_EQ(c.assign().size(), 1u);
  EXPECT_EQ(c.on_status(f), JobOutcome::kDeadLettered);
  EXPECT_EQ(c.scheduler().dlq_count(), 1u);
}
