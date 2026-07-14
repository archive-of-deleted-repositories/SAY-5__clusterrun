#include <cstdio>

#include "core/job_codec.hpp"

// Emits the canonical encodings the golden files under tests/golden/ are
// generated from. Run it and redirect to refresh them after a codec change:
//
//   ./build/tests/codec_dump job > tests/golden/job.json
//   ./build/tests/codec_dump capability > tests/golden/capability.json
//
// The same fixtures are consumed by tests/test_codec_golden.cpp and by the Go
// client's round-trip test in cli/, which is what keeps the two languages
// honest about the wire schema.
using namespace clusterrun;

namespace {

Job sample_job() {
  Job job;
  job.job_id = 42;
  job.command = {"./solve", "--n", "1000000"};
  job.stdout_s3 = "s3://results/42.out";
  job.stderr_s3 = "s3://results/42.err";
  job.reqs.cpu_min = 4;
  job.reqs.mem_gb_min = 8;
  job.reqs.features = {"avx2"};
  job.reqs.node_class = "cpu-heavy";
  job.attempts_max = 3;
  job.checkpoint_interval_s = 10;
  job.idempotency_key = "solve-1000000";
  job.resume_from_checkpoint_s3 = "s3://ck/42/10";
  return job;
}

Capability sample_capability() {
  Capability cap;
  cap.worker_id = 7;
  cap.cpu = 16;
  cap.mem_gb = 64;
  cap.features = {"avx2", "gpu"};
  cap.node_class = "aws";
  return cap;
}

}  // namespace

int main(int argc, char** argv) {
  std::string which = argc > 1 ? argv[1] : "job";
  if (which == "job") {
    std::printf("%s\n", encode_job(sample_job()).c_str());
    return 0;
  }
  if (which == "capability") {
    std::printf("%s\n", encode_capability(sample_capability()).c_str());
    return 0;
  }
  std::fprintf(stderr, "usage: codec_dump [job|capability]\n");
  return 2;
}
