#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>

#include "controller/control_server.hpp"
#include "controller/controller.hpp"
#include "controller/runtime.hpp"
#include "core/version.hpp"
#include "transport/filesystem.hpp"
#include "transport/localstack.hpp"

namespace {

std::uint64_t now_us() {
  using namespace std::chrono;
  return static_cast<std::uint64_t>(
      duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}

std::uint16_t control_port() {
  const char* v = std::getenv("CLUSTERRUN_CONTROL_PORT");
  return v != nullptr ? static_cast<std::uint16_t>(std::strtoul(v, nullptr, 10)) : 8447;
}

}  // namespace

// Long-running controller against LocalStack. The deterministic logic lives in
// controller_step; this is the thin driver that loops it with a real clock. A
// control server (see PROTOCOL.md) shares the controller under one mutex so
// external clients can submit and inspect jobs without touching the queues.
int main() {
  using namespace clusterrun;
  std::printf("clusterrun controller %s endpoint=%s\n", version(), localstack_endpoint().c_str());

  LocalStackQueue queue;
  QueueNames names;
  Controller ctrl;
  std::mutex mu;

  // CLUSTERRUN_BACKEND picks where result artifacts live: "local" means the
  // shared-filesystem store, anything else the S3-shaped store.
  const char* backend = std::getenv("CLUSTERRUN_BACKEND");
  LocalStackObjectStore s3_store;
  FilesystemObjectStore fs_store;
  ObjectStore* store = backend != nullptr && std::string(backend) == "local"
                           ? static_cast<ObjectStore*>(&fs_store)
                           : static_cast<ObjectStore*>(&s3_store);

  ControlServer control(ctrl, mu, control_port(), store);
  if (!control.start()) {
    std::fprintf(stderr, "failed to bind control port %u\n", control_port());
    return 1;
  }
  std::printf("control endpoint on port %u\n", control.port());
  std::fflush(stdout);

  while (true) {
    bool worked = false;
    {
      std::lock_guard<std::mutex> lk(mu);
      worked = controller_step(ctrl, queue, names, now_us());
    }
    if (!worked) {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
  }
  return 0;
}
