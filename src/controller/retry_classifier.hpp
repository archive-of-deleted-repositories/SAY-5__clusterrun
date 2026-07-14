#pragma once

#include <cstdint>
#include <string>

namespace clusterrun {

// What the controller should do with a finished-but-not-successful attempt.
enum class RetryDecision {
  kRetry,       // transient: try again on another worker
  kDeadLetter,  // permanent: do not retry, send to the DLQ
};

// The signals available when an attempt ends without success.
struct AttemptResult {
  int exit_code = 0;               // process exit status (valid when !signaled)
  int term_signal = 0;             // signal number if the process was killed
  bool oom_killed = false;         // the kernel OOM killer reaped the process
  bool deadline_exceeded = false;  // the controller's deadline elapsed first
};

// Maps an attempt outcome to a retry decision. The policy:
//   - deadline_exceeded -> retry (a slower worker or transient slowness)
//   - oom_killed        -> dead-letter (rerunning will OOM the same way)
//   - killed by SIGKILL/SIGTERM (and not OOM) -> retry (worker loss/preemption)
//   - killed by any other signal (SIGSEGV, SIGABRT, ...) -> dead-letter (a bug)
//   - exit code 0       -> retry guard: treated as transient, should not occur
//   - exit code in [1,127] application error -> dead-letter
//   - exit code 128+ (shell-encoded signal) -> dead-letter
RetryDecision classify_retry(const AttemptResult& r);

// A structured failure category. Workers prefix their failure detail strings
// with the category token ("app-error: exit code 3") so the controller, the
// DLQ, and external clients can bucket failures without re-parsing free text.
enum class ErrorCategory : std::uint8_t {
  kNone = 0,       // not a failure
  kAppError,       // clean non-zero exit: deterministic application error
  kCrash,          // killed by a crashing signal (SIGSEGV, SIGABRT, ...)
  kOom,            // reaped by the kernel OOM killer
  kPreempted,      // killed by SIGKILL/SIGTERM: worker preemption or loss
  kDeadline,       // the controller's deadline elapsed
  kSpawnFailure,   // fork/exec itself failed on the worker
  kUploadFailure,  // output upload to the object store failed
  kLost,           // the worker went silent mid-run
};

// Canonical lowercase token for a category ("app-error", "oom", ...).
const char* to_string(ErrorCategory c);

// Buckets an attempt outcome into its failure category. Mirrors the
// classify_retry policy so categories and retry decisions stay consistent.
ErrorCategory categorize(const AttemptResult& r);

// Recovers the category from a failure detail string of the form
// "<category>: <message>". Returns kNone if no known category token leads.
ErrorCategory parse_category(const std::string& detail);

}  // namespace clusterrun
