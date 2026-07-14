#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include "controller/runtime.hpp"  // for QueueNames
#include "core/version.hpp"
#include "transport/filesystem.hpp"
#include "transport/localstack.hpp"
#include "worker/worker.hpp"

namespace {

std::uint32_t env_u32(const char* key, std::uint32_t fallback) {
  const char* v = std::getenv(key);
  return v != nullptr ? static_cast<std::uint32_t>(std::strtoul(v, nullptr, 10)) : fallback;
}

std::string env_str(const char* key, const std::string& fallback) {
  const char* v = std::getenv(key);
  return v != nullptr ? std::string(v) : fallback;
}

}  // namespace

// Long-running worker against LocalStack. Identity and advertised capability come
// from CLUSTERRUN_* environment variables so several workers can run as separate
// processes in CI.
int main() {
  using namespace clusterrun;
  QueueNames names;

  WorkerConfig cfg;
  cfg.capability.worker_id = static_cast<WorkerId>(env_u32("CLUSTERRUN_WORKER_ID", 1));
  cfg.capability.cpu = env_u32("CLUSTERRUN_CPU", 4);
  cfg.capability.mem_gb = env_u32("CLUSTERRUN_MEM_GB", 8);
  cfg.capability.node_class = env_str("CLUSTERRUN_NODE_CLASS", "local");
  if (const char* feats = std::getenv("CLUSTERRUN_FEATURES")) {
    std::string s(feats);
    std::size_t start = 0;
    while (start < s.size()) {
      std::size_t comma = s.find(',', start);
      std::string f = s.substr(start, comma - start);
      if (!f.empty()) {
        cfg.capability.features.push_back(f);
      }
      if (comma == std::string::npos) {
        break;
      }
      start = comma + 1;
    }
  }

  LocalStackQueue queue;
  // CLUSTERRUN_BACKEND=local stores artifacts on a shared filesystem instead
  // of the S3-shaped store; the controller must be started the same way.
  LocalStackObjectStore s3_store;
  FilesystemObjectStore fs_store;
  ObjectStore& store = env_str("CLUSTERRUN_BACKEND", "aws") == "local"
                           ? static_cast<ObjectStore&>(fs_store)
                           : static_cast<ObjectStore&>(s3_store);
  cfg.status_queue_url = queue.ensure_queue(names.status);
  cfg.heartbeat_queue_url = queue.ensure_queue(names.heartbeat);
  std::string claim_url =
      queue.ensure_queue(names.claims_prefix + std::to_string(cfg.capability.worker_id));

  std::printf("clusterrun worker %s id=%u class=%s\n", version(), cfg.capability.worker_id,
              cfg.capability.node_class.c_str());

  while (true) {
    bool ran = worker_step(cfg, queue, store, claim_url);
    if (!ran) {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
  }
  return 0;
}
