#pragma once

#include <string>
#include <vector>

namespace clusterrun {

struct ExecResult {
  bool spawned = false;     // false if fork/exec itself failed
  int exit_code = -1;       // process exit status, valid when !signaled
  int term_signal = 0;      // signal that killed the process, 0 if none
  bool signaled = false;    // true if the process was terminated by a signal
  std::string stdout_data;  // captured standard output
  std::string stderr_data;  // captured standard error
};

// Forks and execs command[0] with command[1..] as argv, capturing stdout and
// stderr fully into memory. command[0] is resolved via PATH. Blocks until the
// child exits. Optional environment entries (KEY=VALUE) are added for the child,
// which the worker uses to pass a resume checkpoint key.
ExecResult run_process(const std::vector<std::string>& command,
                       const std::vector<std::string>& extra_env = {});

}  // namespace clusterrun
