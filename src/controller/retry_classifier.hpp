#pragma once

#include <cstdint>

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

}  // namespace clusterrun
