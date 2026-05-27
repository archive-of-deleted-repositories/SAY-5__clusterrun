#include "worker/worker.hpp"

#include <chrono>

#include "core/job_codec.hpp"
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

  ExecResult r = run_process(job.command, extra_env);
  if (!r.spawned) {
    sink(make_update(job, worker_id, Status::kFailed, 0, "fork/exec failed"));
    return Status::kFailed;
  }

  bool uploaded = put_to_uri(store, job.stdout_s3, r.stdout_data) &&
                  put_to_uri(store, job.stderr_s3, r.stderr_data);

  if (r.signaled) {
    sink(make_update(job, worker_id, Status::kFailed, 0,
                     "terminated by signal " + std::to_string(r.term_signal)));
    return Status::kFailed;
  }
  if (r.exit_code != 0) {
    sink(make_update(job, worker_id, Status::kFailed, 0,
                     "exit code " + std::to_string(r.exit_code)));
    return Status::kFailed;
  }
  if (!uploaded) {
    sink(make_update(job, worker_id, Status::kFailed, 0, "output upload failed"));
    return Status::kFailed;
  }
  sink(make_update(job, worker_id, Status::kSucceeded, 100, ""));
  return Status::kSucceeded;
}

}  // namespace clusterrun
