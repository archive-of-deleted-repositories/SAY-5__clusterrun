#include "controller/retry_classifier.hpp"

#include <csignal>

namespace clusterrun {

RetryDecision classify_retry(const AttemptResult& r) {
  // A blown deadline is most often transient slowness or a slow worker; give the
  // job another chance on a different worker.
  if (r.deadline_exceeded) {
    return RetryDecision::kRetry;
  }
  // Out-of-memory is deterministic for a given input: a rerun would OOM again.
  if (r.oom_killed) {
    return RetryDecision::kDeadLetter;
  }
  // A signaled termination: SIGKILL/SIGTERM usually means the worker was
  // preempted or lost, which is transient. Other signals (SIGSEGV, SIGABRT,
  // SIGBUS, SIGFPE) indicate a crashing program and will recur.
  if (r.term_signal != 0) {
    if (r.term_signal == SIGKILL || r.term_signal == SIGTERM) {
      return RetryDecision::kRetry;
    }
    return RetryDecision::kDeadLetter;
  }
  // A clean non-zero exit is an application error that will recur. Exit code 0
  // should not reach the classifier (that path is success), but if it does treat
  // it as transient rather than dead-lettering a job that arguably succeeded.
  if (r.exit_code == 0) {
    return RetryDecision::kRetry;
  }
  return RetryDecision::kDeadLetter;
}

}  // namespace clusterrun
