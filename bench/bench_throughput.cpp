#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "bench/bench_common.hpp"
#include "controller/controller.hpp"
#include "controller/runtime.hpp"
#include "core/job_codec.hpp"
#include "transport/memory.hpp"
#include "worker/worker.hpp"

using namespace clusterrun;

namespace {

constexpr std::uint64_t kSec = 1000ull * 1000;

Job make_job(JobId id) {
  Job j;
  j.job_id = id;
  j.command = {"true"};  // a near-instant heavy-compute stand-in
  j.attempts_max = 2;
  return j;
}

WorkerConfig make_worker(WorkerId id, MemoryQueue& q, const QueueNames& names) {
  WorkerConfig cfg;
  cfg.capability.worker_id = id;
  cfg.capability.cpu = 16;
  cfg.capability.mem_gb = 64;
  cfg.capability.node_class = "local";
  cfg.status_queue_url = q.ensure_queue(names.status);
  cfg.heartbeat_queue_url = q.ensure_queue(names.heartbeat);
  return cfg;
}

}  // namespace

// Throughput benchmark: submit N jobs to a controller with W workers and measure
// jobs/sec plus a coarse per-stage latency breakdown (submit, dispatch, run).
int main() {
  const std::uint32_t jobs = bench::env_u32("JOBS", 1000);
  const std::uint32_t worker_count = bench::env_u32("WORKERS", 3);

  MemoryQueue queue;
  MemoryObjectStore store;
  QueueNames names;
  Controller ctrl(60 * kSec);

  std::vector<WorkerConfig> workers;
  std::vector<std::string> claim_urls;
  for (std::uint32_t i = 0; i < worker_count; ++i) {
    workers.push_back(make_worker(static_cast<WorkerId>(i + 1), queue, names));
    claim_urls.push_back(queue.ensure_queue(names.claims_prefix +
                                            std::to_string(workers.back().capability.worker_id)));
  }

  std::string jobs_url = queue.ensure_queue(names.jobs);

  std::uint64_t t_submit_start = bench::now_us();
  for (std::uint32_t i = 1; i <= jobs; ++i) {
    queue.send(jobs_url, encode_job(make_job(i)));
  }
  std::uint64_t submit_us = bench::now_us() - t_submit_start;

  std::uint64_t clock = 0;
  std::uint64_t t_run_start = bench::now_us();
  for (int iter = 0; iter < 200000; ++iter) {
    clock += kSec;
    controller_step(ctrl, queue, names, clock);
    for (std::size_t w = 0; w < workers.size(); ++w) {
      worker_step(workers[w], queue, store, claim_urls[w]);
    }
    if (ctrl.scheduler().done_count() + ctrl.scheduler().dlq_count() >= jobs) {
      break;
    }
  }
  std::uint64_t run_us = bench::now_us() - t_run_start;

  std::size_t done = ctrl.scheduler().done_count();
  double secs = static_cast<double>(run_us) / 1e6;
  double jps = secs > 0 ? static_cast<double>(done) / secs : 0.0;

  std::printf("throughput jobs=%u workers=%u done=%zu\n", jobs, worker_count, done);
  std::printf("  submit_total_us=%llu (%.2f us/job)\n", static_cast<unsigned long long>(submit_us),
              jobs ? static_cast<double>(submit_us) / jobs : 0.0);
  std::printf("  run_total_us=%llu run_total_s=%.4f\n", static_cast<unsigned long long>(run_us),
              secs);
  std::printf("  jobs_per_sec=%.0f\n", jps);
  std::printf("  per_job_dispatch_run_us=%.2f\n", done ? static_cast<double>(run_us) / done : 0.0);

  return done == jobs ? 0 : 1;
}
