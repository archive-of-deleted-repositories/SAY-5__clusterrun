#include <cstdint>
#include <cstdio>
#include <set>
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
  j.command = {"true"};
  j.attempts_max = 5;  // generous so churn does not exhaust attempts
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

// Fault-recovery benchmark: run N jobs across a pool of workers and kill one
// worker every 20 completed jobs, immediately replacing it with a fresh id.
// Measures completion rate and the mean time from a worker going silent to its
// orphaned jobs being redriven onto a survivor.
int main() {
  const std::uint32_t jobs = bench::env_u32("JOBS", 200);
  const std::uint64_t timeout = 30 * kSec;

  MemoryQueue queue;
  MemoryObjectStore store;
  QueueNames names;
  Controller ctrl(timeout);

  // Start with three workers; ids climb as we replace killed ones.
  std::vector<WorkerConfig> workers;
  std::vector<std::string> claim_urls;
  std::set<WorkerId> alive;
  WorkerId next_id = 1;
  auto add_worker = [&]() {
    WorkerConfig cfg = make_worker(next_id, queue, names);
    workers.push_back(cfg);
    claim_urls.push_back(queue.ensure_queue(names.claims_prefix + std::to_string(next_id)));
    alive.insert(next_id);
    ++next_id;
  };
  for (int i = 0; i < 3; ++i) {
    add_worker();
  }

  std::string jobs_url = queue.ensure_queue(names.jobs);
  for (std::uint32_t i = 1; i <= jobs; ++i) {
    queue.send(jobs_url, encode_job(make_job(i)));
  }

  std::uint64_t clock = 0;
  std::size_t last_kill_at = 0;
  int kills = 0;
  std::uint64_t total_redrive_us = 0;
  int redrive_samples = 0;

  for (int iter = 0; iter < 500000; ++iter) {
    clock += kSec;
    std::size_t before_inflight = ctrl.scheduler().in_flight_count();

    auto lost = ctrl.tick(clock);  // implicit via controller_step below too
    (void)lost;
    controller_step(ctrl, queue, names, clock);

    for (std::size_t w = 0; w < workers.size(); ++w) {
      if (alive.count(workers[w].capability.worker_id)) {
        worker_step(workers[w], queue, store, claim_urls[w]);
      }
    }

    std::size_t done = ctrl.scheduler().done_count();
    // Every 20 completions, kill the lowest-id live worker and add a new one.
    if (done >= last_kill_at + 20 && alive.size() > 1) {
      WorkerId victim = *alive.begin();
      std::uint64_t silent_at = clock;
      alive.erase(victim);
      ++kills;
      last_kill_at = done;
      add_worker();

      // Measure how long until the controller redrives the victim's jobs: keep
      // stepping until in-flight recovers to its pre-kill level.
      std::uint64_t recovered_at = silent_at;
      for (int k = 0; k < 200000; ++k) {
        clock += kSec;
        controller_step(ctrl, queue, names, clock);
        for (std::size_t w = 0; w < workers.size(); ++w) {
          if (alive.count(workers[w].capability.worker_id)) {
            worker_step(workers[w], queue, store, claim_urls[w]);
          }
        }
        if (ctrl.scheduler().in_flight_count() >= before_inflight ||
            ctrl.scheduler().done_count() + ctrl.scheduler().dlq_count() >= jobs) {
          recovered_at = clock;
          break;
        }
      }
      total_redrive_us += (recovered_at - silent_at);
      ++redrive_samples;
    }

    if (ctrl.scheduler().done_count() + ctrl.scheduler().dlq_count() >= jobs) {
      break;
    }
  }

  std::size_t done = ctrl.scheduler().done_count();
  std::size_t dlq = ctrl.scheduler().dlq_count();
  double completion_rate = jobs ? 100.0 * static_cast<double>(done) / jobs : 0.0;
  double mean_redrive_us =
      redrive_samples ? static_cast<double>(total_redrive_us) / redrive_samples : 0.0;

  std::printf("fault-recovery jobs=%u kills=%d\n", jobs, kills);
  std::printf("  succeeded=%zu dead_lettered=%zu\n", done, dlq);
  std::printf("  completion_rate_pct=%.1f\n", completion_rate);
  std::printf("  mean_time_to_redrive_s=%.1f (simulated clock)\n", mean_redrive_us / 1e6);

  // Pass when no job is silently lost: every job is succeeded or dead-lettered.
  return (done + dlq == jobs) ? 0 : 1;
}
