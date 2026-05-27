#include <cstddef>
#include <cstdint>
#include <string>

#include "core/job_codec.hpp"

// Drives the job JSON decoder with arbitrary bytes. The contract under test:
// decode_job must never crash, leak, or throw on untrusted input, and any job it
// accepts must round-trip through encode_job back to an equivalent job.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  std::string input(reinterpret_cast<const char*>(data), size);
  auto job = clusterrun::decode_job(input);
  if (job) {
    // A decoded job must re-encode and re-decode to the same job_id and command.
    auto again = clusterrun::decode_job(clusterrun::encode_job(*job));
    if (!again || again->job_id != job->job_id || again->command != job->command) {
      __builtin_trap();
    }
  }
  return 0;
}
