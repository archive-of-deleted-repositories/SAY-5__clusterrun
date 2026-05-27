#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

#include "controller/controller.hpp"
#include "controller/runtime.hpp"
#include "core/job_codec.hpp"
#include "fakes.hpp"
#include "worker/worker.hpp"

using namespace clusterrun;

namespace {

constexpr std::uint64_t kSec = 1000ull * 1000;

// Builds a job whose requirements rotate across a few profiles so the scheduler
// has a genuine matching problem to solve.
Job make_mixed_job(JobId id) {
  Job j;
  j.job_id = id;
  j.attempts_max = 3;
  switch (id % 5) {
    case 0:
      j.command = {"true"};  // trivially succeeds
      j.reqs.cpu_min = 2;
      break;
    case 1:
      j.command = {"sh", "-c", "echo job " + std::to_string(id)};
      j.reqs.cpu_min = 4;
      j.reqs.features = {"avx2"};
      break;
    case 2:
      j.command = {"sh", "-c", "exit 1"};  // always fails: should dead-letter
      j.reqs.cpu_min = 2;
      break;
    case 3:
      j.command = {"sh", "-c", "echo big; exit 0"};
      j.reqs.cpu_min = 8;
      j.reqs.mem_gb_min = 16;
      break;
    default:
      j.command = {"echo", "plain"};
      break;
  }
  j.stdout_s3 = "s3://results/" + std::to_string(id) + ".out";
  return j;
}

WorkerConfig make_worker(WorkerId id, std::uint32_t cpu, std::uint32_t mem,
                         std::vector<std::string> feats, const QueueNames& names, Queue& q) {
  WorkerConfig cfg;
  cfg.capability.worker_id = id;
  cfg.capability.cpu = cpu;
  cfg.capability.mem_gb = mem;
  cfg.capability.features = std::move(feats);
  cfg.capability.node_class = "local";
  cfg.status_queue_url = q.ensure_queue(names.status);
  cfg.heartbeat_queue_url = q.ensure_queue(names.heartbeat);
  return cfg;
}

}  // namespace

// Three workers, one controller, fifty mixed-requirement jobs. One worker is
// killed a third of the way through. Every job must reach a terminal state
// (succeeded or dead-lettered) with no silent loss.
TEST(Integration, FiftyJobsThreeWorkersOneKilled) {
  FakeQueue queue;
  FakeObjectStore store;
  QueueNames names;
  Controller ctrl(30 * kSec);

  // Workers: 1 and 2 are large with avx2; 3 is large with avx2 too so the avx2
  // jobs always have a home even after one worker dies.
  std::vector<WorkerConfig> workers = {
      make_worker(1, 16, 64, {"avx2"}, names, queue),
      make_worker(2, 16, 64, {"avx2"}, names, queue),
      make_worker(3, 16, 64, {"avx2"}, names, queue),
  };
  std::vector<std::string> claim_urls;
  for (auto& w : workers) {
    claim_urls.push_back(
        queue.ensure_queue(names.claims_prefix + std::to_string(w.capability.worker_id)));
  }

  std::string jobs_url = queue.ensure_queue(names.jobs);
  const int kJobs = 50;
  for (int i = 1; i <= kJobs; ++i) {
    queue.send(jobs_url, encode_job(make_mixed_job(i)));
  }

  std::uint64_t clock = 0;
  bool killed = false;
  std::set<WorkerId> dead;

  // Drive the system to quiescence. Each iteration advances the clock by 1s so
  // that once a worker is killed and stops beating, loss detection eventually
  // fires.
  for (int iter = 0; iter < 4000; ++iter) {
    clock += 1 * kSec;

    controller_step(ctrl, queue, names, clock);

    // Worker passes. A killed worker neither beats nor runs.
    for (std::size_t w = 0; w < workers.size(); ++w) {
      if (dead.count(workers[w].capability.worker_id)) {
        continue;
      }
      worker_step(workers[w], queue, store, claim_urls[w]);
    }

    // Kill worker 1 once the controller has dispatched a meaningful chunk.
    if (!killed && ctrl.scheduler().done_count() + ctrl.scheduler().dlq_count() >= 12) {
      dead.insert(1);
      killed = true;
    }

    if (ctrl.scheduler().done_count() + ctrl.scheduler().dlq_count() >=
        static_cast<std::size_t>(kJobs)) {
      break;
    }
  }

  EXPECT_TRUE(killed);
  std::size_t done = ctrl.scheduler().done_count();
  std::size_t dlq = ctrl.scheduler().dlq_count();
  // Every job reached a terminal state: no silent losses.
  EXPECT_EQ(done + dlq, static_cast<std::size_t>(kJobs));
  // The always-failing profile (id % 5 == 2) dead-letters: ids 2,7,...,47 = 10.
  EXPECT_EQ(dlq, 10u);
  EXPECT_EQ(done, 40u);

  // Successful jobs with a stdout sink uploaded an object.
  EXPECT_GT(store.object_count(), 0u);
}
