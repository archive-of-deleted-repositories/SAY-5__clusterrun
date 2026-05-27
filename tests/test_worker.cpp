#include <gtest/gtest.h>

#include <vector>

#include "core/types.hpp"
#include "fakes.hpp"
#include "worker/exec.hpp"
#include "worker/worker.hpp"

using namespace clusterrun;

namespace {

Job make_job(JobId id, std::vector<std::string> cmd) {
  Job j;
  j.job_id = id;
  j.command = std::move(cmd);
  j.stdout_s3 = "s3://results/" + std::to_string(id) + ".out";
  j.stderr_s3 = "s3://results/" + std::to_string(id) + ".err";
  j.attempts_max = 1;
  return j;
}

}  // namespace

TEST(Exec, CapturesStdout) {
  auto r = run_process({"echo", "hello"});
  ASSERT_TRUE(r.spawned);
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_data, "hello\n");
}

TEST(Exec, NonZeroExit) {
  auto r = run_process({"sh", "-c", "exit 3"});
  ASSERT_TRUE(r.spawned);
  EXPECT_FALSE(r.signaled);
  EXPECT_EQ(r.exit_code, 3);
}

TEST(Exec, Signaled) {
  auto r = run_process({"sh", "-c", "kill -9 $$"});
  ASSERT_TRUE(r.spawned);
  EXPECT_TRUE(r.signaled);
  EXPECT_EQ(r.term_signal, 9);
}

TEST(Worker, ExecuteJobSuccessUploadsOutput) {
  FakeObjectStore store;
  std::vector<Status> seen;
  auto sink = [&](const StatusUpdate& u) { seen.push_back(u.status); };

  Status final = execute_job(make_job(1, {"sh", "-c", "echo out; echo err 1>&2"}), 5, store, sink);
  EXPECT_EQ(final, Status::kSucceeded);

  ASSERT_GE(seen.size(), 3u);
  EXPECT_EQ(seen.front(), Status::kClaimed);
  EXPECT_EQ(seen.back(), Status::kSucceeded);

  auto out = store.get("results", "1.out");
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(*out, "out\n");
  auto err = store.get("results", "1.err");
  ASSERT_TRUE(err.has_value());
  EXPECT_EQ(*err, "err\n");
}

TEST(Worker, ExecuteJobFailurePropagates) {
  FakeObjectStore store;
  Status last = Status::kClaimed;
  auto sink = [&](const StatusUpdate& u) { last = u.status; };
  Status final = execute_job(make_job(2, {"sh", "-c", "exit 1"}), 5, store, sink);
  EXPECT_EQ(final, Status::kFailed);
  EXPECT_EQ(last, Status::kFailed);
}

TEST(Worker, HeartbeatRoundTrips) {
  Capability c;
  c.worker_id = 9;
  c.cpu = 32;
  c.mem_gb = 128;
  c.features = {"avx2"};
  c.node_class = "cpu-heavy";
  std::string hb = make_heartbeat(c);
  EXPECT_NE(hb.find("\"worker_id\":9"), std::string::npos);
  EXPECT_NE(hb.find("cpu-heavy"), std::string::npos);
}
