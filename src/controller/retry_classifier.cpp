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

const char* to_string(ErrorCategory c) {
  switch (c) {
    case ErrorCategory::kNone:
      return "none";
    case ErrorCategory::kAppError:
      return "app-error";
    case ErrorCategory::kCrash:
      return "crash";
    case ErrorCategory::kOom:
      return "oom";
    case ErrorCategory::kPreempted:
      return "preempted";
    case ErrorCategory::kDeadline:
      return "deadline";
    case ErrorCategory::kSpawnFailure:
      return "spawn-failure";
    case ErrorCategory::kUploadFailure:
      return "upload-failure";
    case ErrorCategory::kLost:
      return "lost";
  }
  return "none";
}

ErrorCategory categorize(const AttemptResult& r) {
  if (r.deadline_exceeded) {
    return ErrorCategory::kDeadline;
  }
  if (r.oom_killed) {
    return ErrorCategory::kOom;
  }
  if (r.term_signal != 0) {
    if (r.term_signal == SIGKILL || r.term_signal == SIGTERM) {
      return ErrorCategory::kPreempted;
    }
    return ErrorCategory::kCrash;
  }
  if (r.exit_code == 0) {
    return ErrorCategory::kNone;
  }
  return ErrorCategory::kAppError;
}

ErrorCategory parse_category(const std::string& detail) {
  static constexpr ErrorCategory kAll[] = {
      ErrorCategory::kAppError,      ErrorCategory::kCrash,    ErrorCategory::kOom,
      ErrorCategory::kPreempted,     ErrorCategory::kDeadline, ErrorCategory::kSpawnFailure,
      ErrorCategory::kUploadFailure, ErrorCategory::kLost,
  };
  for (ErrorCategory c : kAll) {
    std::string prefix = std::string(to_string(c)) + ":";
    if (detail.rfind(prefix, 0) == 0) {
      return c;
    }
  }
  return ErrorCategory::kNone;
}

}  // namespace clusterrun
