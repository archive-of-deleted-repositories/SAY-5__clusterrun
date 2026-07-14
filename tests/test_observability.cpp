#include <gtest/gtest.h>

#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "controller/control_server.hpp"
#include "controller/controller.hpp"
#include "controller/scheduler.hpp"

using namespace clusterrun;
using nlohmann::json;

namespace {

constexpr std::uint64_t kMs = 1000;

Capability cap(WorkerId id) {
  Capability c;
  c.worker_id = id;
  c.cpu = 8;
  c.mem_gb = 16;
  return c;
}

Job job(JobId id, std::uint32_t attempts_max = 1) {
  Job j;
  j.job_id = id;
  j.command = {"true"};
  j.attempts_max = attempts_max;
  return j;
}

StatusUpdate update(JobId id, Status s, std::uint64_t ts_us, std::string detail = "") {
  StatusUpdate u;
  u.job_id = id;
  u.status = s;
  u.ts_us = ts_us;
  u.detail = std::move(detail);
  return u;
}

// Drives one job through claim and success with a fixed latency.
void complete(Scheduler& s, JobId id, std::uint64_t start_us, std::uint64_t latency_us) {
  s.on_status(update(id, Status::kClaimed, start_us));
  s.on_status(update(id, Status::kSucceeded, start_us + latency_us));
}

}  // namespace

TEST(Drain, DrainedWorkerGetsNothingNew) {
  Scheduler s;
  s.upsert_worker(cap(2));
  s.submit(job(1));
  auto first = s.assign();
  ASSERT_EQ(first.size(), 1u);
  EXPECT_EQ(first[0].first, 2);

  // Worker 2 drains mid-run; worker 1 arrives to take the new work.
  s.upsert_worker(cap(1));
  EXPECT_TRUE(s.drain_worker(2));
  EXPECT_TRUE(s.is_draining(2));
  s.submit(job(2));
  s.submit(job(3));
  s.submit(job(4));
  for (auto& [worker, claimed] : s.assign()) {
    EXPECT_EQ(worker, 1) << "job " << claimed.job_id << " went to the draining worker";
  }

  // The drained worker still finishes what it already holds.
  const JobState* st = s.job_state(1);
  ASSERT_NE(st, nullptr);
  EXPECT_EQ(st->assigned_to, std::optional<WorkerId>(2));
  EXPECT_EQ(s.on_status(update(1, Status::kSucceeded, 5 * kMs)), JobOutcome::kDone);
}

TEST(Drain, HeartbeatDoesNotClearDraining) {
  Scheduler s;
  s.upsert_worker(cap(1));
  EXPECT_TRUE(s.drain_worker(1));
  s.upsert_worker(cap(1));  // the next heartbeat refreshes capability
  EXPECT_TRUE(s.is_draining(1));
  s.submit(job(1));
  EXPECT_TRUE(s.assign().empty());
}

TEST(Drain, UnknownWorkerIsRejected) {
  Scheduler s;
  EXPECT_FALSE(s.drain_worker(9));

  Controller ctrl;
  std::mutex mu;
  ControlServer server(ctrl, mu, 0);
  EXPECT_EQ(server.route("POST", "/v1/workers/9/drain", "{}").status, 404);
  EXPECT_EQ(server.route("POST", "/v1/workers/abc/drain", "{}").status, 400);
}

TEST(Drain, RouteMarksWorkerDraining) {
  Controller ctrl;
  std::mutex mu;
  ControlServer server(ctrl, mu, 0);
  ctrl.on_heartbeat(cap(3), 0);

  ControlResponse r = server.route("POST", "/v1/workers/3/drain", "{}");
  ASSERT_EQ(r.status, 200);
  json j = json::parse(r.body);
  EXPECT_EQ(j["worker_id"], 3);
  EXPECT_EQ(j["draining"], true);
  EXPECT_TRUE(ctrl.scheduler().is_draining(3));
}

TEST(Stats, MatchesADeterministicRun) {
  Scheduler s;
  s.upsert_worker(cap(1));

  // Three successes with 100ms, 200ms, and 1s latencies.
  s.submit(job(1));
  s.submit(job(2));
  s.submit(job(3, 2));
  s.assign();
  complete(s, 1, 1000 * kMs, 100 * kMs);
  complete(s, 2, 1100 * kMs, 1000 * kMs);
  // Job 3 is preempted once, then succeeds on the retry.
  s.on_status(update(3, Status::kClaimed, 1200 * kMs));
  s.on_status(update(3, Status::kFailed, 1300 * kMs, "preempted: terminated by signal 15"));
  s.assign();
  complete(s, 3, 1400 * kMs, 200 * kMs);
  // Job 4 dead-letters immediately.
  s.submit(job(4));
  s.assign();
  s.on_status(update(4, Status::kClaimed, 1500 * kMs));
  s.on_status(update(4, Status::kFailed, 1600 * kMs, "app-error: exit code 3"));

  SchedulerStats st = s.stats();
  EXPECT_EQ(st.submitted, 4u);
  EXPECT_EQ(st.done, 3u);
  EXPECT_EQ(st.dead_lettered, 1u);
  ASSERT_EQ(st.retries_by_category.size(), 1u);
  EXPECT_EQ(st.retries_by_category.at("preempted"), 1u);
  // Sorted latencies: 100ms, 200ms, 1000ms.
  EXPECT_EQ(st.latency_p50_us, 200 * kMs);
  EXPECT_EQ(st.latency_p95_us, 1000 * kMs);
  // 3 completions between the first claim (t=1000ms) and last success (t=2100ms).
  EXPECT_NEAR(st.throughput_per_s, 3.0 / 1.1, 0.01);
}

TEST(Stats, RouteExposesTheSnapshot) {
  Controller ctrl;
  std::mutex mu;
  ControlServer server(ctrl, mu, 0);
  ctrl.on_heartbeat(cap(1), 0);
  ctrl.submit(job(1));
  ctrl.assign();
  ctrl.on_status(update(1, Status::kClaimed, 1000 * kMs));
  ctrl.on_status(update(1, Status::kSucceeded, 1250 * kMs));

  ControlResponse r = server.route("GET", "/v1/stats", "");
  ASSERT_EQ(r.status, 200);
  json j = json::parse(r.body);
  EXPECT_EQ(j["submitted"], 1);
  EXPECT_EQ(j["done"], 1);
  EXPECT_EQ(j["dead_lettered"], 0);
  EXPECT_EQ(j["latency_p50_us"], 250 * kMs);
  EXPECT_EQ(j["retries_by_category"], json::object());
}
