#include <gtest/gtest.h>

#include "controller/match.hpp"
#include "controller/scheduler.hpp"

using namespace clusterrun;

namespace {

Capability worker(WorkerId id, std::uint32_t cpu, std::uint32_t mem,
                  std::vector<std::string> feats = {}, std::string klass = "") {
  Capability c;
  c.worker_id = id;
  c.cpu = cpu;
  c.mem_gb = mem;
  c.features = std::move(feats);
  c.node_class = std::move(klass);
  return c;
}

Job job(JobId id, std::uint32_t cpu = 0, std::uint32_t mem = 0, std::vector<std::string> feats = {},
        std::string klass = "") {
  Job j;
  j.job_id = id;
  j.command = {"true"};
  j.reqs.cpu_min = cpu;
  j.reqs.mem_gb_min = mem;
  j.reqs.features = std::move(feats);
  j.reqs.node_class = std::move(klass);
  j.attempts_max = 3;
  return j;
}

StatusUpdate status(JobId id, Status s, WorkerId w, std::string detail = "") {
  StatusUpdate u;
  u.job_id = id;
  u.status = s;
  u.worker_id = w;
  u.detail = std::move(detail);
  return u;
}

}  // namespace

TEST(Match, CpuMemFeaturesNodeClass) {
  EXPECT_TRUE(capability_matches(job(1, 4, 8).reqs, worker(1, 8, 16)));
  EXPECT_FALSE(capability_matches(job(1, 16, 8).reqs, worker(1, 8, 16)));
  EXPECT_FALSE(capability_matches(job(1, 4, 32).reqs, worker(1, 8, 16)));
  EXPECT_TRUE(capability_matches(job(1, 0, 0, {"avx2"}).reqs, worker(1, 8, 16, {"avx2", "gpu"})));
  EXPECT_FALSE(capability_matches(job(1, 0, 0, {"gpu"}).reqs, worker(1, 8, 16, {"avx2"})));
  EXPECT_TRUE(capability_matches(job(1, 0, 0, {}, "aws").reqs, worker(1, 8, 16, {}, "aws")));
  EXPECT_FALSE(capability_matches(job(1, 0, 0, {}, "gpu").reqs, worker(1, 8, 16, {}, "aws")));
}

TEST(Scheduler, HappyPathLifecycle) {
  Scheduler s;
  s.upsert_worker(worker(1, 8, 16));
  s.submit(job(100));
  auto claims = s.assign();
  ASSERT_EQ(claims.size(), 1u);
  EXPECT_EQ(claims[0].first, 1);
  EXPECT_EQ(claims[0].second.job_id, 100u);
  EXPECT_EQ(s.in_flight_count(), 1u);

  EXPECT_EQ(s.on_status(status(100, Status::kRunning, 1)), JobOutcome::kInProgress);
  EXPECT_EQ(s.on_status(status(100, Status::kSucceeded, 1)), JobOutcome::kDone);
  EXPECT_EQ(s.done_count(), 1u);
  EXPECT_EQ(s.in_flight_count(), 0u);
}

TEST(Scheduler, NoDoubleClaim) {
  Scheduler s;
  s.upsert_worker(worker(1, 8, 16));
  s.submit(job(1));
  auto first = s.assign();
  ASSERT_EQ(first.size(), 1u);
  // A second assign pass must not hand the same job out again.
  auto second = s.assign();
  EXPECT_TRUE(second.empty());
}

TEST(Scheduler, RoundRobinFewestLoad) {
  Scheduler s;
  s.upsert_worker(worker(1, 8, 16));
  s.upsert_worker(worker(2, 8, 16));
  s.submit(job(1));
  s.submit(job(2));
  s.submit(job(3));
  s.submit(job(4));
  auto claims = s.assign();
  ASSERT_EQ(claims.size(), 4u);
  // Each worker should get two jobs given equal eligibility.
  int w1 = 0, w2 = 0;
  for (auto& c : claims) {
    (c.first == 1 ? w1 : w2)++;
  }
  EXPECT_EQ(w1, 2);
  EXPECT_EQ(w2, 2);
}

TEST(Scheduler, RedriveThenDeadLetter) {
  Scheduler s;
  s.upsert_worker(worker(1, 8, 16));
  Job j = job(7);
  j.attempts_max = 2;
  s.submit(j);

  ASSERT_EQ(s.assign().size(), 1u);  // attempt 1
  EXPECT_EQ(s.on_status(status(7, Status::kFailed, 1)), JobOutcome::kRedriven);
  ASSERT_EQ(s.assign().size(), 1u);  // attempt 2
  EXPECT_EQ(s.on_status(status(7, Status::kFailed, 1)), JobOutcome::kDeadLettered);
  EXPECT_EQ(s.dlq_count(), 1u);
  ASSERT_EQ(s.dead_letters().size(), 1u);
  EXPECT_EQ(s.dead_letters()[0], 7u);
}

TEST(Scheduler, LostWorkerRedrivesJob) {
  Scheduler s;
  s.upsert_worker(worker(1, 8, 16));
  s.upsert_worker(worker(2, 8, 16));
  s.submit(job(1));
  auto claims = s.assign();
  ASSERT_EQ(claims.size(), 1u);
  WorkerId holder = claims[0].first;

  s.remove_worker(holder);
  // The job should be back in the pending set and reassignable to the survivor.
  auto reassigned = s.assign();
  ASSERT_EQ(reassigned.size(), 1u);
  EXPECT_NE(reassigned[0].first, holder);
}

TEST(Scheduler, PendingCapabilityWhenNoMatch) {
  Scheduler s;
  s.upsert_worker(worker(1, 2, 4));  // small worker
  s.submit(job(1, 64, 256));         // needs a big worker
  auto claims = s.assign();
  EXPECT_TRUE(claims.empty());
  auto waiting = s.waiting_for();
  ASSERT_EQ(waiting.size(), 1u);
  EXPECT_NE(waiting.at(1).find("cpu>=64"), std::string::npos);

  // A matching worker arriving should unblock the job.
  s.upsert_worker(worker(2, 128, 512));
  auto after = s.assign();
  ASSERT_EQ(after.size(), 1u);
  EXPECT_EQ(after[0].first, 2);
}
