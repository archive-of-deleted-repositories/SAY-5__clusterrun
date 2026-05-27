#include <gtest/gtest.h>

#include <csignal>

#include "controller/retry_classifier.hpp"

using namespace clusterrun;

namespace {

AttemptResult exited(int code) {
  AttemptResult r;
  r.exit_code = code;
  return r;
}

AttemptResult signaled(int sig) {
  AttemptResult r;
  r.term_signal = sig;
  return r;
}

}  // namespace

// Decision table over (exit_code, signal, oom_killed, deadline_exceeded). Each
// row pins the policy so a future change to classify_retry is caught here.
struct Row {
  AttemptResult result;
  RetryDecision expected;
  const char* label;
};

TEST(RetryClassifier, DecisionTable) {
  std::vector<Row> rows = {
      {[] {
         AttemptResult r;
         r.deadline_exceeded = true;
         return r;
       }(),
       RetryDecision::kRetry, "deadline exceeded retries"},
      {[] {
         AttemptResult r;
         r.oom_killed = true;
         return r;
       }(),
       RetryDecision::kDeadLetter, "oom dead-letters"},
      // deadline takes precedence over oom when both are set.
      {[] {
         AttemptResult r;
         r.oom_killed = true;
         r.deadline_exceeded = true;
         return r;
       }(),
       RetryDecision::kRetry, "deadline beats oom"},
      {signaled(SIGKILL), RetryDecision::kRetry, "sigkill (preemption) retries"},
      {signaled(SIGTERM), RetryDecision::kRetry, "sigterm (preemption) retries"},
      {signaled(SIGSEGV), RetryDecision::kDeadLetter, "segfault dead-letters"},
      {signaled(SIGABRT), RetryDecision::kDeadLetter, "abort dead-letters"},
      {signaled(SIGFPE), RetryDecision::kDeadLetter, "fpe dead-letters"},
      {exited(1), RetryDecision::kDeadLetter, "exit 1 dead-letters"},
      {exited(2), RetryDecision::kDeadLetter, "exit 2 dead-letters"},
      {exited(127), RetryDecision::kDeadLetter, "exit 127 dead-letters"},
      {exited(0), RetryDecision::kRetry, "exit 0 guard retries"},
      // oom_killed dominates a signal kill: rerunning OOMs again.
      {[] {
         AttemptResult r;
         r.term_signal = SIGKILL;
         r.oom_killed = true;
         return r;
       }(),
       RetryDecision::kDeadLetter, "oom kill dead-letters despite sigkill"},
  };

  for (const auto& row : rows) {
    EXPECT_EQ(classify_retry(row.result), row.expected) << row.label;
  }
}
