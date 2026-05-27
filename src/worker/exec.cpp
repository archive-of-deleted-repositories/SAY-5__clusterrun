#include "worker/exec.hpp"

#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <vector>

extern char** environ;

namespace clusterrun {

namespace {

// Drains a pipe read end fully into out. Returns when EOF is reached.
void drain(int fd, std::string* out) {
  std::array<char, 4096> buf;
  ssize_t n;
  while ((n = ::read(fd, buf.data(), buf.size())) > 0) {
    out->append(buf.data(), static_cast<std::size_t>(n));
  }
}

}  // namespace

ExecResult run_process(const std::vector<std::string>& command,
                       const std::vector<std::string>& extra_env) {
  ExecResult result;
  if (command.empty()) {
    return result;
  }

  int out_pipe[2];
  int err_pipe[2];
  if (::pipe(out_pipe) != 0 || ::pipe(err_pipe) != 0) {
    return result;
  }

  pid_t pid = ::fork();
  if (pid < 0) {
    ::close(out_pipe[0]);
    ::close(out_pipe[1]);
    ::close(err_pipe[0]);
    ::close(err_pipe[1]);
    return result;
  }

  if (pid == 0) {
    // Child: wire stdout/stderr to the pipes and exec.
    ::dup2(out_pipe[1], STDOUT_FILENO);
    ::dup2(err_pipe[1], STDERR_FILENO);
    ::close(out_pipe[0]);
    ::close(out_pipe[1]);
    ::close(err_pipe[0]);
    ::close(err_pipe[1]);

    for (const auto& kv : extra_env) {
      ::putenv(const_cast<char*>(kv.c_str()));
    }

    std::vector<char*> argv;
    argv.reserve(command.size() + 1);
    for (const auto& a : command) {
      argv.push_back(const_cast<char*>(a.c_str()));
    }
    argv.push_back(nullptr);
    ::execvp(argv[0], argv.data());
    // execvp only returns on failure.
    ::_exit(127);
  }

  // Parent: close write ends, drain both pipes, reap the child.
  ::close(out_pipe[1]);
  ::close(err_pipe[1]);
  result.spawned = true;
  drain(out_pipe[0], &result.stdout_data);
  drain(err_pipe[0], &result.stderr_data);
  ::close(out_pipe[0]);
  ::close(err_pipe[0]);

  int wstatus = 0;
  while (::waitpid(pid, &wstatus, 0) < 0 && errno == EINTR) {
  }
  if (WIFEXITED(wstatus)) {
    result.exit_code = WEXITSTATUS(wstatus);
  } else if (WIFSIGNALED(wstatus)) {
    result.signaled = true;
    result.term_signal = WTERMSIG(wstatus);
  }
  return result;
}

}  // namespace clusterrun
