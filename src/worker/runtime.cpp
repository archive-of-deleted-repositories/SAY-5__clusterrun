#include "core/base64.hpp"
#include "core/job_codec.hpp"
#include "core/status_codec.hpp"
#include "worker/worker.hpp"

namespace clusterrun {

bool worker_step(const WorkerConfig& cfg, Queue& queue, ObjectStore& store,
                 const std::string& claim_queue_url) {
  // Announce liveness and capability every pass.
  queue.send(cfg.heartbeat_queue_url, make_heartbeat(cfg.capability));

  auto msgs = queue.receive(claim_queue_url, 1, 1);
  if (msgs.empty()) {
    return false;
  }

  auto job = decode_job(msgs[0].body);
  if (!job) {
    queue.remove(claim_queue_url, msgs[0].receipt);
    return false;
  }

  // Hand the resume checkpoint key to the child process, if any.
  std::vector<std::string> extra_env;
  if (!job->resume_from_checkpoint_s3.empty()) {
    extra_env.push_back("CLUSTERRUN_RESUME_FROM=" + job->resume_from_checkpoint_s3);
  }

  auto sink = [&](const StatusUpdate& u) {
    queue.send(cfg.status_queue_url, base64_encode(encode_status(u)));
  };
  execute_job(*job, cfg.capability.worker_id, store, sink, extra_env);
  queue.remove(claim_queue_url, msgs[0].receipt);
  return true;
}

}  // namespace clusterrun
