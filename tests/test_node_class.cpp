#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

#include "controller/scheduler.hpp"

using namespace clusterrun;

namespace {

Capability worker(WorkerId id, const std::string& klass, std::vector<std::string> feats = {}) {
  Capability c;
  c.worker_id = id;
  c.cpu = 16;
  c.mem_gb = 64;
  c.node_class = klass;
  c.features = std::move(feats);
  return c;
}

Job job(JobId id, const std::string& klass, std::vector<std::string> feats = {}) {
  Job j;
  j.job_id = id;
  j.command = {"true"};
  j.reqs.node_class = klass;
  j.reqs.features = std::move(feats);
  j.attempts_max = 3;
  return j;
}

}  // namespace

// Four workers, one per class. Each job targets one class and must run only on
// the worker of that class.
TEST(NodeClass, EachJobRunsOnItsClass) {
  Scheduler s;
  s.upsert_worker(worker(1, "aws"));
  s.upsert_worker(worker(2, "local"));
  s.upsert_worker(worker(3, "gpu", {"gpu"}));
  s.upsert_worker(worker(4, "cpu-heavy"));

  const std::map<std::string, WorkerId> expected = {
      {"aws", 1}, {"local", 2}, {"gpu", 3}, {"cpu-heavy", 4}};

  JobId id = 100;
  for (const auto& [klass, wid] : expected) {
    s.submit(klass == "gpu" ? job(id, klass, {"gpu"}) : job(id, klass));
    auto claims = s.assign();
    ASSERT_EQ(claims.size(), 1u) << "class " << klass;
    EXPECT_EQ(claims[0].first, wid) << "job for class " << klass << " ran on wrong worker";
    // Mark it done so the next round starts clean.
    StatusUpdate done;
    done.job_id = id;
    done.status = Status::kSucceeded;
    done.worker_id = wid;
    s.on_status(done);
    ++id;
  }
}

// A gpu job with no gpu worker present must wait in pending_capability with a
// waiting_for reason that names the missing class, rather than running on a
// non-gpu worker.
TEST(NodeClass, GpuJobWaitsWithoutGpuWorker) {
  Scheduler s;
  s.upsert_worker(worker(1, "aws"));
  s.upsert_worker(worker(2, "cpu-heavy"));

  s.submit(job(7, "gpu", {"gpu"}));
  auto claims = s.assign();
  EXPECT_TRUE(claims.empty()) << "gpu job must not run on a non-gpu worker";

  auto waiting = s.waiting_for();
  ASSERT_EQ(waiting.size(), 1u);
  ASSERT_TRUE(waiting.count(7));
  EXPECT_NE(waiting.at(7).find("node_class:gpu"), std::string::npos);
  EXPECT_NE(waiting.at(7).find("feature:gpu"), std::string::npos);

  // When a gpu worker arrives, the job runs on it.
  s.upsert_worker(worker(3, "gpu", {"gpu"}));
  auto after = s.assign();
  ASSERT_EQ(after.size(), 1u);
  EXPECT_EQ(after[0].first, 3);
  EXPECT_TRUE(s.waiting_for().empty());
}

// A job with an empty node_class is class-agnostic: it may run on any worker.
TEST(NodeClass, EmptyClassRunsAnywhere) {
  Scheduler s;
  s.upsert_worker(worker(1, "aws"));
  Job j = job(1, "");  // no class requirement
  s.submit(j);
  auto claims = s.assign();
  ASSERT_EQ(claims.size(), 1u);
  EXPECT_EQ(claims[0].first, 1);
}
