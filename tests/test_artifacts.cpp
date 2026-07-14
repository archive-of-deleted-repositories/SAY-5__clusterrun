#include <gtest/gtest.h>

#include <unistd.h>

#include <cstdlib>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "controller/control_server.hpp"
#include "controller/controller.hpp"
#include "core/sha256.hpp"
#include "transport/filesystem.hpp"
#include "transport/memory.hpp"
#include "worker/worker.hpp"

using namespace clusterrun;
using nlohmann::json;

namespace {

constexpr const char* kHelloSha =
    "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824";

std::string temp_root() {
  char tmpl[] = "/tmp/clusterrun-fs-test-XXXXXX";
  return ::mkdtemp(tmpl);
}

Job result_job(JobId id, const std::string& payload) {
  Job j;
  j.job_id = id;
  j.command = {"sh", "-c", "printf '" + payload + "' > \"$CLUSTERRUN_RESULT_FILE\""};
  j.result_s3 = "s3://results/" + std::to_string(id) + ".bin";
  return j;
}

}  // namespace

TEST(Sha256, KnownVectors) {
  EXPECT_EQ(sha256_hex(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  EXPECT_EQ(sha256_hex("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  // 56-byte message exercises the two-block padding path.
  EXPECT_EQ(sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  EXPECT_EQ(sha256_hex("hello"), kHelloSha);
}

TEST(FilesystemStore, RoundTripsNestedKeys) {
  FilesystemObjectStore store(temp_root());
  EXPECT_TRUE(store.ensure_bucket("results"));
  EXPECT_TRUE(store.put("results", "42/attempt-1.bin", "payload"));
  auto got = store.get("results", "42/attempt-1.bin");
  ASSERT_TRUE(got.has_value());
  EXPECT_EQ(*got, "payload");
  EXPECT_FALSE(store.get("results", "missing").has_value());
}

TEST(FilesystemStore, RejectsPathEscapes) {
  FilesystemObjectStore store(temp_root());
  EXPECT_FALSE(store.put("results", "../escape", "x"));
  EXPECT_FALSE(store.put("..", "key", "x"));
  EXPECT_FALSE(store.get("results", "a/../../escape").has_value());
}

TEST(Artifacts, WorkerUploadsChecksummedResult) {
  MemoryObjectStore store;
  std::vector<StatusUpdate> updates;
  Job job = result_job(7, "hello");
  Status final = execute_job(job, 1, store, [&](const StatusUpdate& u) { updates.push_back(u); });
  EXPECT_EQ(final, Status::kSucceeded);
  ASSERT_FALSE(updates.empty());
  EXPECT_EQ(updates.back().detail, "result: s3://results/7.bin sha256:" + std::string(kHelloSha));
  auto stored = store.get("results", "7.bin");
  ASSERT_TRUE(stored.has_value());
  EXPECT_EQ(*stored, "hello");
}

TEST(Artifacts, MissingResultFileFailsAsUploadFailure) {
  MemoryObjectStore store;
  std::vector<StatusUpdate> updates;
  Job job = result_job(8, "x");
  job.command = {"true"};  // never writes the result file
  Status final = execute_job(job, 1, store, [&](const StatusUpdate& u) { updates.push_back(u); });
  EXPECT_EQ(final, Status::kFailed);
  EXPECT_EQ(updates.back().detail, "upload-failure: result file not produced");
}

TEST(Artifacts, SchedulerRecordsArtifactFromSuccessDetail) {
  Scheduler s;
  Capability cap;
  cap.worker_id = 1;
  cap.cpu = 8;
  s.upsert_worker(cap);
  Job job = result_job(9, "hello");
  s.submit(job);
  s.assign();
  StatusUpdate u;
  u.job_id = 9;
  u.status = Status::kSucceeded;
  u.worker_id = 1;
  u.detail = "result: s3://results/9.bin sha256:" + std::string(kHelloSha);
  EXPECT_EQ(s.on_status(u), JobOutcome::kDone);
  const JobState* st = s.job_state(9);
  ASSERT_NE(st, nullptr);
  EXPECT_EQ(st->result_uri, "s3://results/9.bin");
  EXPECT_EQ(st->result_sha256, kHelloSha);
}

TEST(Artifacts, ControlServerServesVerifiableDownload) {
  Controller ctrl;
  std::mutex mu;
  MemoryObjectStore store;
  ControlServer server(ctrl, mu, 0, &store);

  MemoryObjectStore worker_store;
  Job job = result_job(3, "hello");
  ctrl.submit(job);
  store.ensure_bucket("results");
  store.put("results", "3.bin", "hello");
  StatusUpdate u;
  u.job_id = 3;
  u.status = Status::kSucceeded;
  u.detail = "result: s3://results/3.bin sha256:" + std::string(kHelloSha);
  ctrl.on_status(u);

  ControlResponse meta = server.route("GET", "/v1/jobs/3", "");
  ASSERT_EQ(meta.status, 200);
  json j = json::parse(meta.body);
  EXPECT_EQ(j["artifact"], "s3://results/3.bin");
  EXPECT_EQ(j["artifact_sha256"], kHelloSha);

  ControlResponse blob = server.route("GET", "/v1/jobs/3/result", "");
  ASSERT_EQ(blob.status, 200);
  EXPECT_EQ(blob.body, "hello");
  EXPECT_EQ(blob.content_type, "application/octet-stream");
  EXPECT_EQ(blob.checksum, kHelloSha);
  EXPECT_EQ(sha256_hex(blob.body), blob.checksum);

  ControlResponse missing = server.route("GET", "/v1/jobs/999/result", "");
  EXPECT_EQ(missing.status, 404);
}
