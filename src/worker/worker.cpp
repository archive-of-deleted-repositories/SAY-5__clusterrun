#include "worker/worker.hpp"

#include <unistd.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "core/job_codec.hpp"
#include "core/sha256.hpp"
#include "transport/localstack.hpp"
#include "worker/exec.hpp"

namespace clusterrun {

namespace {

std::uint64_t now_us() {
  using namespace std::chrono;
  return static_cast<std::uint64_t>(
      duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}

StatusUpdate make_update(const Job& job, WorkerId worker_id, Status s, std::uint32_t progress,
                         std::string detail) {
  StatusUpdate u;
  u.job_id = job.job_id;
  u.status = s;
  u.ts_us = now_us();
  u.progress = progress;
  u.worker_id = worker_id;
  u.detail = std::move(detail);
  return u;
}

// Writes data to the bucket/key named by an s3:// uri. A no-op (treated as
// success) when the uri is empty so jobs without output sinks still complete.
bool put_to_uri(ObjectStore& store, const std::string& uri, const std::string& data) {
  if (uri.empty()) {
    return true;
  }
  std::string bucket, key;
  if (!parse_s3_uri(uri, &bucket, &key)) {
    return false;
  }
  store.ensure_bucket(bucket);
  return store.put(bucket, key, data);
}

}  // namespace

std::string make_heartbeat(const Capability& cap) {
  return encode_capability(cap);
}

Status execute_job(const Job& job, WorkerId worker_id, ObjectStore& store, const StatusSink& sink,
                   const std::vector<std::string>& extra_env) {
  sink(make_update(job, worker_id, Status::kClaimed, 0, ""));
  sink(make_update(job, worker_id, Status::kRunning, 0, ""));

  // Jobs with a result destination get a scratch path to write their artifact.
  std::vector<std::string> env = extra_env;
  std::string result_path;
  if (!job.result_s3.empty()) {
    result_path =
        "/tmp/clusterrun-result-" + std::to_string(job.job_id) + "-" + std::to_string(::getpid());
    env.push_back("CLUSTERRUN_RESULT_FILE=" + result_path);
  }

  ExecResult r = run_process(job.command, env);
  if (!r.spawned) {
    sink(make_update(job, worker_id, Status::kFailed, 0, "spawn-failure: fork/exec failed"));
    return Status::kFailed;
  }

  bool uploaded = put_to_uri(store, job.stdout_s3, r.stdout_data) &&
                  put_to_uri(store, job.stderr_s3, r.stderr_data);

  // Failure details lead with the ErrorCategory token (see retry_classifier.hpp)
  // so the controller and external clients can bucket failures structurally.
  if (r.signaled) {
    bool preempted = r.term_signal == SIGKILL || r.term_signal == SIGTERM;
    std::string category = preempted ? "preempted" : "crash";
    sink(make_update(job, worker_id, Status::kFailed, 0,
                     category + ": terminated by signal " + std::to_string(r.term_signal)));
    return Status::kFailed;
  }
  if (r.exit_code != 0) {
    sink(make_update(job, worker_id, Status::kFailed, 0,
                     "app-error: exit code " + std::to_string(r.exit_code)));
    return Status::kFailed;
  }
  if (!uploaded) {
    sink(make_update(job, worker_id, Status::kFailed, 0, "upload-failure: output upload failed"));
    return Status::kFailed;
  }

  // Upload the checksummed result artifact, if the job declared one. The
  // sha256 travels in the success detail so the controller can serve it to
  // clients for download verification.
  std::string success_detail;
  if (!job.result_s3.empty()) {
    std::ifstream in(result_path, std::ios::binary);
    if (!in.good()) {
      sink(make_update(job, worker_id, Status::kFailed, 0,
                       "upload-failure: result file not produced"));
      return Status::kFailed;
    }
    std::ostringstream data;
    data << in.rdbuf();
    in.close();
    std::remove(result_path.c_str());
    if (!put_to_uri(store, job.result_s3, data.str())) {
      sink(make_update(job, worker_id, Status::kFailed, 0,
                       "upload-failure: result artifact upload failed"));
      return Status::kFailed;
    }
    success_detail = "result: " + job.result_s3 + " sha256:" + sha256_hex(data.str());
  }
  sink(make_update(job, worker_id, Status::kSucceeded, 100, success_detail));
  return Status::kSucceeded;
}

}  // namespace clusterrun
