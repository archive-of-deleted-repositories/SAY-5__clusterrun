#include <gtest/gtest.h>

#include "core/job_codec.hpp"
#include "core/status_codec.hpp"
#include "core/types.hpp"

using namespace clusterrun;

namespace {

Job sample_job() {
  Job j;
  j.job_id = 42;
  j.command = {"./solve", "--n", "1000000"};
  j.stdout_s3 = "s3://results/42.out";
  j.stderr_s3 = "s3://results/42.err";
  j.reqs.cpu_min = 4;
  j.reqs.mem_gb_min = 8;
  j.reqs.features = {"avx2"};
  j.reqs.node_class = "cpu-heavy";
  j.attempts_max = 3;
  j.checkpoint_interval_s = 10;
  j.idempotency_key = "solve-1000000";
  return j;
}

}  // namespace

TEST(JobCodec, RoundTrip) {
  Job in = sample_job();
  auto out = decode_job(encode_job(in));
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->job_id, in.job_id);
  EXPECT_EQ(out->command, in.command);
  EXPECT_EQ(out->stdout_s3, in.stdout_s3);
  EXPECT_EQ(out->reqs.cpu_min, 4u);
  EXPECT_EQ(out->reqs.mem_gb_min, 8u);
  EXPECT_EQ(out->reqs.features, std::vector<std::string>{"avx2"});
  EXPECT_EQ(out->reqs.node_class, "cpu-heavy");
  EXPECT_EQ(out->attempts_max, 3u);
  EXPECT_EQ(out->checkpoint_interval_s, 10u);
  EXPECT_EQ(out->idempotency_key, "solve-1000000");
}

TEST(JobCodec, RejectsMissingJobId) {
  EXPECT_FALSE(decode_job(R"({"command":["x"]})").has_value());
}

TEST(JobCodec, RejectsEmptyCommand) {
  EXPECT_FALSE(decode_job(R"({"job_id":1,"command":[]})").has_value());
}

TEST(JobCodec, RejectsGarbage) {
  EXPECT_FALSE(decode_job("not json at all").has_value());
  EXPECT_FALSE(decode_job("").has_value());
  EXPECT_FALSE(decode_job("[]").has_value());
}

TEST(JobCodec, DefaultsAttemptsToOne) {
  auto out = decode_job(R"({"job_id":1,"command":["x"]})");
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->attempts_max, 1u);
}

TEST(JobCodec, RejectsWrongTypeRequires) {
  EXPECT_FALSE(decode_job(R"({"job_id":1,"command":["x"],"requires":7})").has_value());
}

TEST(CapabilityCodec, RoundTrip) {
  Capability c;
  c.worker_id = 5;
  c.cpu = 16;
  c.mem_gb = 64;
  c.features = {"avx2", "gpu"};
  c.node_class = "gpu";
  auto out = decode_capability(encode_capability(c));
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->worker_id, 5);
  EXPECT_EQ(out->cpu, 16u);
  EXPECT_EQ(out->mem_gb, 64u);
  EXPECT_EQ(out->features.size(), 2u);
  EXPECT_EQ(out->node_class, "gpu");
}

TEST(StatusCodec, RoundTrip) {
  StatusUpdate u;
  u.job_id = 99;
  u.status = Status::kRunning;
  u.ts_us = 1234567890123ull;
  u.progress = 55;
  u.worker_id = 7;
  u.detail = "checkpoint=s3://ck/99/3";
  auto buf = encode_status(u);
  auto out = decode_status(buf);
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->job_id, 99u);
  EXPECT_EQ(out->status, Status::kRunning);
  EXPECT_EQ(out->ts_us, 1234567890123ull);
  EXPECT_EQ(out->progress, 55u);
  EXPECT_EQ(out->worker_id, 7);
  EXPECT_EQ(out->detail, "checkpoint=s3://ck/99/3");
}

TEST(StatusCodec, EmptyDetail) {
  StatusUpdate u;
  u.job_id = 1;
  u.status = Status::kSucceeded;
  auto out = decode_status(encode_status(u));
  ASSERT_TRUE(out.has_value());
  EXPECT_TRUE(out->detail.empty());
}

TEST(StatusCodec, RejectsTruncated) {
  StatusUpdate u;
  u.job_id = 1;
  u.detail = "abc";
  auto buf = encode_status(u);
  buf.pop_back();  // drop a detail byte: len now disagrees with size
  EXPECT_FALSE(decode_status(buf).has_value());
}

TEST(StatusCodec, RejectsBadStatusByte) {
  StatusUpdate u;
  u.job_id = 1;
  auto buf = encode_status(u);
  buf[4 + 8] = 0xfe;  // overwrite the status byte with an invalid value
  EXPECT_FALSE(decode_status(buf).has_value());
}

TEST(StatusCodec, RejectsShortBuffer) {
  std::vector<std::uint8_t> buf = {1, 2, 3};
  EXPECT_FALSE(decode_status(buf).has_value());
}
