#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "controller/scheduler.hpp"
#include "core/job_codec.hpp"

using namespace clusterrun;

namespace {

Capability cap(WorkerId id) {
  Capability c;
  c.worker_id = id;
  c.cpu = 8;
  c.mem_gb = 16;
  c.node_class = "local";
  return c;
}

Job job(JobId id, Priority p = Priority::kNormal, std::string tenant = "") {
  Job j;
  j.job_id = id;
  j.command = {"true"};
  j.priority = p;
  j.tenant = std::move(tenant);
  return j;
}

std::vector<JobId> claim_order(Scheduler& s) {
  std::vector<JobId> order;
  for (auto& [worker, claimed] : s.assign()) {
    order.push_back(claimed.job_id);
  }
  return order;
}

}  // namespace

TEST(Priority, HighOvertakesQueuedNormals) {
  Scheduler s;
  s.upsert_worker(cap(1));
  s.submit(job(1));
  s.submit(job(2));
  s.submit(job(3));
  s.submit(job(99, Priority::kHigh));  // arrives last, dispatches first
  std::vector<JobId> order = claim_order(s);
  ASSERT_EQ(order.size(), 4u);
  EXPECT_EQ(order[0], 99u);
}

TEST(Priority, LowDispatchesAfterEveryNormal) {
  Scheduler s;
  s.upsert_worker(cap(1));
  s.submit(job(1, Priority::kLow));
  s.submit(job(2));
  s.submit(job(3));
  std::vector<JobId> expected = {2, 3, 1};
  EXPECT_EQ(claim_order(s), expected);
}

TEST(Priority, TenantsAlternateWithinOneBand) {
  Scheduler s;
  s.upsert_worker(cap(1));
  // Tenant a floods the queue before tenant b shows up; b must not starve.
  s.submit(job(1, Priority::kNormal, "a"));
  s.submit(job(2, Priority::kNormal, "a"));
  s.submit(job(3, Priority::kNormal, "a"));
  s.submit(job(4, Priority::kNormal, "a"));
  s.submit(job(5, Priority::kNormal, "b"));
  s.submit(job(6, Priority::kNormal, "b"));
  std::vector<JobId> expected = {1, 5, 2, 6, 3, 4};
  EXPECT_EQ(claim_order(s), expected);
}

TEST(Priority, FairnessSurvivesCapabilityRequeue) {
  Scheduler s;
  // No worker yet: everything parks in pending_capability.
  s.submit(job(1, Priority::kNormal, "a"));
  s.submit(job(2, Priority::kNormal, "a"));
  s.submit(job(3, Priority::kNormal, "b"));
  s.submit(job(9, Priority::kHigh, "a"));
  EXPECT_TRUE(s.assign().empty());
  EXPECT_EQ(s.waiting_for().size(), 4u);
  // A worker arrives: the high job leads and tenants still alternate.
  s.upsert_worker(cap(1));
  std::vector<JobId> expected = {9, 1, 3, 2};
  EXPECT_EQ(claim_order(s), expected);
}

TEST(Priority, PendingCountsPerBand) {
  Scheduler s;
  s.submit(job(1, Priority::kHigh));
  s.submit(job(2));
  s.submit(job(3));
  s.submit(job(4, Priority::kLow));
  EXPECT_EQ(s.pending_count(), 4u);
  EXPECT_EQ(s.pending_count(Priority::kHigh), 1u);
  EXPECT_EQ(s.pending_count(Priority::kNormal), 2u);
  EXPECT_EQ(s.pending_count(Priority::kLow), 1u);
}

TEST(Priority, CodecCarriesPriorityAndTenant) {
  Job in = job(7, Priority::kLow, "acme");
  auto out = decode_job(encode_job(in));
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->priority, Priority::kLow);
  EXPECT_EQ(out->tenant, "acme");
}

TEST(Priority, CodecDefaultsToNormal) {
  auto out = decode_job(R"({"job_id": 1, "command": ["true"]})");
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->priority, Priority::kNormal);
  EXPECT_EQ(out->tenant, "");
}

TEST(Priority, CodecRejectsUnknownPriority) {
  EXPECT_FALSE(decode_job(R"({"job_id": 1, "command": ["true"], "priority": "urgent"})"));
  EXPECT_FALSE(decode_job(R"({"job_id": 1, "command": ["true"], "priority": 2})"));
  EXPECT_FALSE(decode_job(R"({"job_id": 1, "command": ["true"], "tenant": 5})"));
}
