#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>

#include "controller/controller.hpp"
#include "controller/runtime.hpp"
#include "core/version.hpp"
#include "transport/localstack.hpp"

namespace {

std::uint64_t now_us() {
  using namespace std::chrono;
  return static_cast<std::uint64_t>(
      duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}

}  // namespace

// Long-running controller against LocalStack. The deterministic logic lives in
// controller_step; this is the thin driver that loops it with a real clock.
int main() {
  using namespace clusterrun;
  std::printf("clusterrun controller %s endpoint=%s\n", version(), localstack_endpoint().c_str());

  LocalStackQueue queue;
  QueueNames names;
  Controller ctrl;

  while (true) {
    bool worked = controller_step(ctrl, queue, names, now_us());
    if (!worked) {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
  }
  return 0;
}
