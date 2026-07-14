#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>

#include "core/job_codec.hpp"

// Pins the codec to the committed golden fixtures under tests/golden/. The Go
// client round-trips the same files, so a codec change that breaks the cross
// language contract fails here and in cli/ together. Regenerate with the
// codec_dump tool (see tests/codec_dump.cpp) when the schema changes on
// purpose.

using namespace clusterrun;

namespace {

std::string read_golden(const std::string& name) {
  std::ifstream in(std::string(CLUSTERRUN_GOLDEN_DIR) + "/" + name);
  EXPECT_TRUE(in.good()) << "missing golden " << name;
  std::ostringstream out;
  out << in.rdbuf();
  std::string text = out.str();
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
    text.pop_back();
  }
  return text;
}

}  // namespace

TEST(CodecGolden, JobRoundTripsByteForByte) {
  std::string golden = read_golden("job.json");
  auto job = decode_job(golden);
  ASSERT_TRUE(job.has_value());
  EXPECT_EQ(encode_job(*job), golden);
}

TEST(CodecGolden, JobGoldenFieldsSurvive) {
  auto job = decode_job(read_golden("job.json"));
  ASSERT_TRUE(job.has_value());
  EXPECT_EQ(job->job_id, 42u);
  EXPECT_EQ(job->command.size(), 3u);
  EXPECT_EQ(job->reqs.node_class, "cpu-heavy");
  EXPECT_EQ(job->attempts_max, 3u);
  EXPECT_EQ(job->resume_from_checkpoint_s3, "s3://ck/42/10");
}

TEST(CodecGolden, CapabilityRoundTripsByteForByte) {
  std::string golden = read_golden("capability.json");
  auto cap = decode_capability(golden);
  ASSERT_TRUE(cap.has_value());
  EXPECT_EQ(encode_capability(*cap), golden);
}
