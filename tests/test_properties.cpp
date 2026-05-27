#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <random>
#include <set>
#include <vector>

#include "controller/match.hpp"
#include "controller/scheduler.hpp"

using namespace clusterrun;

namespace {

const std::vector<std::string> kFeaturePool = {"avx2", "gpu", "fma", "sve"};

Capability random_worker(std::mt19937& rng, WorkerId id) {
  Capability c;
  c.worker_id = id;
  c.cpu = 1 + rng() % 64;
  c.mem_gb = 1 + rng() % 256;
  for (const auto& f : kFeaturePool) {
    if (rng() % 2) {
      c.features.push_back(f);
    }
  }
  return c;
}

Requirements random_reqs(std::mt19937& rng) {
  Requirements r;
  r.cpu_min = rng() % 16;
  r.mem_gb_min = rng() % 64;
  for (const auto& f : kFeaturePool) {
    if (rng() % 4 == 0) {
      r.features.push_back(f);
    }
  }
  return r;
}

}  // namespace

// Property: a worker the matcher accepts genuinely satisfies every declared
// requirement; one it rejects violates at least one.
TEST(Property, MatchIsConsistentWithRequirements) {
  std::mt19937 rng(1234);
  for (int trial = 0; trial < 5000; ++trial) {
    Capability w = random_worker(rng, 1);
    Requirements r = random_reqs(rng);
    bool matched = capability_matches(r, w);

    bool cpu_ok = w.cpu >= r.cpu_min;
    bool mem_ok = w.mem_gb >= r.mem_gb_min;
    bool feat_ok = true;
    for (const auto& f : r.features) {
      if (std::find(w.features.begin(), w.features.end(), f) == w.features.end()) {
        feat_ok = false;
      }
    }
    bool expected = cpu_ok && mem_ok && feat_ok;
    EXPECT_EQ(matched, expected) << "trial " << trial;
  }
}

// Property: across random submit/assign rounds, no job is ever claimed by two
// workers at once, and every claim goes to an eligible worker.
TEST(Property, NoDoubleClaimAndAlwaysEligible) {
  std::mt19937 rng(99);
  for (int trial = 0; trial < 400; ++trial) {
    Scheduler s;
    std::vector<Capability> workers;
    int worker_count = 1 + rng() % 5;
    for (int i = 0; i < worker_count; ++i) {
      Capability w = random_worker(rng, static_cast<WorkerId>(i + 1));
      workers.push_back(w);
      s.upsert_worker(w);
    }

    int job_count = rng() % 20;
    for (int j = 0; j < job_count; ++j) {
      Job job;
      job.job_id = static_cast<JobId>(j + 1);
      job.command = {"true"};
      job.reqs = random_reqs(rng);
      job.attempts_max = 3;
      s.submit(job);
    }

    auto claims = s.assign();
    std::set<JobId> claimed_jobs;
    for (auto& [wid, job] : claims) {
      // No job appears twice in one assignment pass.
      EXPECT_TRUE(claimed_jobs.insert(job.job_id).second) << "double claim job " << job.job_id;
      // The assigned worker must actually be eligible.
      const Capability* cap = nullptr;
      for (auto& w : workers) {
        if (w.worker_id == wid) {
          cap = &w;
        }
      }
      ASSERT_NE(cap, nullptr);
      EXPECT_TRUE(capability_matches(job.reqs, *cap)) << "ineligible claim job " << job.job_id;
    }
    // A second pass yields nothing new: claimed jobs are not re-handed-out.
    EXPECT_TRUE(s.assign().empty());
  }
}

// Property: with N identical workers and K jobs of equal eligibility, claim
// counts differ by at most one (round-robin fairness via fewest-load tie-break).
TEST(Property, RoundRobinBalancesEqualWorkers) {
  std::mt19937 rng(7);
  for (int trial = 0; trial < 200; ++trial) {
    Scheduler s;
    int worker_count = 2 + rng() % 4;
    for (int i = 0; i < worker_count; ++i) {
      Capability w;
      w.worker_id = static_cast<WorkerId>(i + 1);
      w.cpu = 64;
      w.mem_gb = 256;
      s.upsert_worker(w);
    }
    int job_count = worker_count * (1 + rng() % 5);
    for (int j = 0; j < job_count; ++j) {
      Job job;
      job.job_id = static_cast<JobId>(j + 1);
      job.command = {"true"};
      s.submit(job);
    }
    auto claims = s.assign();
    std::map<WorkerId, int> load;
    for (auto& [wid, job] : claims) {
      load[wid]++;
    }
    int lo = job_count, hi = 0;
    for (auto& [wid, n] : load) {
      lo = std::min(lo, n);
      hi = std::max(hi, n);
    }
    EXPECT_LE(hi - lo, 1) << "imbalance in trial " << trial;
  }
}
