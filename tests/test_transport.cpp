#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

#include "transport/http_client.hpp"
#include "transport/localstack.hpp"

using namespace clusterrun;

namespace {

// Transport tests need a live LocalStack. They run in CI (where it is always up)
// and locally when CLUSTERRUN_LIVE=1 is set; otherwise they are skipped so the
// default unit build stays hermetic.
bool live() {
  const char* v = std::getenv("CLUSTERRUN_LIVE");
  return v != nullptr && std::string(v) == "1";
}

std::string unique(const std::string& prefix) {
  return prefix + "-" + std::to_string(::getpid()) + "-" +
         std::to_string(::testing::UnitTest::GetInstance()->random_seed());
}

}  // namespace

TEST(UrlEncode, EncodesReserved) {
  EXPECT_EQ(url_encode("a b&c=d"), "a%20b%26c%3Dd");
  EXPECT_EQ(url_encode("plain.AZ-09_~"), "plain.AZ-09_~");
}

TEST(ParseS3Uri, ParsesBucketAndKey) {
  std::string b, k;
  ASSERT_TRUE(parse_s3_uri("s3://results/path/to/42.out", &b, &k));
  EXPECT_EQ(b, "results");
  EXPECT_EQ(k, "path/to/42.out");
}

TEST(ParseS3Uri, RejectsBad) {
  std::string b, k;
  EXPECT_FALSE(parse_s3_uri("http://x/y", &b, &k));
  EXPECT_FALSE(parse_s3_uri("s3://nokey", &b, &k));
  EXPECT_FALSE(parse_s3_uri("s3:///onlyslash", &b, &k));
}

TEST(LocalStackQueue, SendReceiveDelete) {
  if (!live()) {
    GTEST_SKIP() << "set CLUSTERRUN_LIVE=1 with LocalStack running";
  }
  LocalStackQueue q;
  std::string url = q.ensure_queue(unique("jobs"));
  ASSERT_FALSE(url.empty());
  ASSERT_TRUE(q.send(url, R"({"job_id":1,"command":["echo"]})"));
  auto msgs = q.receive(url, 1, 2);
  ASSERT_EQ(msgs.size(), 1u);
  EXPECT_EQ(msgs[0].body, R"({"job_id":1,"command":["echo"]})");
  EXPECT_TRUE(q.remove(url, msgs[0].receipt));
}

TEST(LocalStackObjectStore, PutGet) {
  if (!live()) {
    GTEST_SKIP() << "set CLUSTERRUN_LIVE=1 with LocalStack running";
  }
  LocalStackObjectStore s;
  std::string bucket = unique("results");
  ASSERT_TRUE(s.ensure_bucket(bucket));
  ASSERT_TRUE(s.put(bucket, "42.out", "hello world"));
  auto got = s.get(bucket, "42.out");
  ASSERT_TRUE(got.has_value());
  EXPECT_EQ(*got, "hello world");
  EXPECT_FALSE(s.get(bucket, "missing").has_value());
}
