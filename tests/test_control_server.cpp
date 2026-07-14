#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <mutex>
#include <nlohmann/json.hpp>

#include "controller/control_server.hpp"
#include "controller/controller.hpp"

using namespace clusterrun;
using nlohmann::json;

namespace {

Capability cap(WorkerId id, std::uint32_t cpu = 8) {
  Capability c;
  c.worker_id = id;
  c.cpu = cpu;
  c.mem_gb = 16;
  c.node_class = "local";
  return c;
}

StatusUpdate update(JobId id, Status s, WorkerId w, std::string detail = "") {
  StatusUpdate u;
  u.job_id = id;
  u.status = s;
  u.worker_id = w;
  u.detail = std::move(detail);
  return u;
}

class ControlServerTest : public ::testing::Test {
 protected:
  ControlServerTest() : server_(ctrl_, mu_, 0) {
  }

  json get(const std::string& path, int expect_status = 200) {
    ControlResponse r = server_.route("GET", path, "");
    EXPECT_EQ(r.status, expect_status) << path << " -> " << r.body;
    return json::parse(r.body);
  }

  json post(const std::string& path, const std::string& body, int expect_status = 200) {
    ControlResponse r = server_.route("POST", path, body);
    EXPECT_EQ(r.status, expect_status) << path << " -> " << r.body;
    return json::parse(r.body);
  }

  Controller ctrl_;
  std::mutex mu_;
  ControlServer server_;
};

}  // namespace

TEST_F(ControlServerTest, HealthzReportsOk) {
  json j = get("/v1/healthz");
  EXPECT_EQ(j["status"], "ok");
  EXPECT_TRUE(j.contains("version"));
}

TEST_F(ControlServerTest, SubmitThenLifecycle) {
  json sub = post("/v1/jobs", R"({"job_id":1,"command":["echo","hi"],"attempts_max":2})");
  EXPECT_EQ(sub["job_id"], 1);
  EXPECT_EQ(sub["state"], "pending");

  json st = get("/v1/jobs/1");
  EXPECT_EQ(st["state"], "pending");
  EXPECT_EQ(st["attempts"], 0);

  ctrl_.on_heartbeat(cap(3), 0);
  auto claims = ctrl_.assign();
  ASSERT_EQ(claims.size(), 1u);
  EXPECT_EQ(claims[0].first, 3);

  st = get("/v1/jobs/1");
  EXPECT_EQ(st["state"], "claimed");
  EXPECT_EQ(st["worker_id"], 3);

  ctrl_.on_status(update(1, Status::kRunning, 3));
  EXPECT_EQ(get("/v1/jobs/1")["state"], "running");

  ctrl_.on_status(update(1, Status::kSucceeded, 3));
  EXPECT_EQ(get("/v1/jobs/1")["state"], "succeeded");
}

TEST_F(ControlServerTest, FailureSurfacesCategory) {
  post("/v1/jobs", R"({"job_id":5,"command":["false"],"attempts_max":1})");
  ctrl_.on_heartbeat(cap(1), 0);
  ctrl_.assign();
  ctrl_.on_status(update(5, Status::kFailed, 1, "app-error: exit code 1"));

  json st = get("/v1/jobs/5");
  EXPECT_EQ(st["state"], "dead_lettered");
  EXPECT_EQ(st["category"], "app-error");
  EXPECT_EQ(st["detail"], "app-error: exit code 1");
}

TEST_F(ControlServerTest, RejectsBadInput) {
  post("/v1/jobs", "not json", 400);
  post("/v1/jobs", R"({"command":["x"]})", 400);  // missing job_id
  post("/v1/jobs", R"({"job_id":7,"command":["x"]})");
  post("/v1/jobs", R"({"job_id":7,"command":["x"]})", 400);  // duplicate
  get("/v1/jobs/999", 404);
  get("/v1/jobs/abc", 400);
  get("/v1/nope", 404);
}

TEST_F(ControlServerTest, QueueDepthsAndWaiting) {
  post("/v1/jobs", R"({"job_id":1,"command":["x"]})");
  post("/v1/jobs", R"({"job_id":2,"command":["x"],"requires":{"features":["gpu"]}})");
  ctrl_.on_heartbeat(cap(1), 0);
  ctrl_.assign();

  json q = get("/v1/queue");
  EXPECT_EQ(q["pending"], 0);
  EXPECT_EQ(q["in_flight"], 1);
  EXPECT_EQ(q["done"], 0);
  ASSERT_TRUE(q["waiting"].contains("2"));
  EXPECT_EQ(get("/v1/jobs/2")["state"], "waiting");
}

// End to end over a real socket: bind an ephemeral port and speak HTTP/1.1.
TEST_F(ControlServerTest, ServesOverSocket) {
  ASSERT_TRUE(server_.start());
  ASSERT_GT(server_.port(), 0);

  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(fd, 0);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(server_.port());
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ASSERT_EQ(::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);

  std::string body = R"({"job_id":9,"command":["echo"]})";
  std::string req = "POST /v1/jobs HTTP/1.1\r\nHost: localhost\r\nContent-Length: " +
                    std::to_string(body.size()) + "\r\n\r\n" + body;
  ASSERT_EQ(::send(fd, req.data(), req.size(), 0), static_cast<ssize_t>(req.size()));

  std::string resp;
  char buf[2048];
  ssize_t n;
  while ((n = ::recv(fd, buf, sizeof(buf), 0)) > 0) {
    resp.append(buf, static_cast<std::size_t>(n));
  }
  ::close(fd);

  EXPECT_NE(resp.find("HTTP/1.1 200 OK"), std::string::npos) << resp;
  EXPECT_NE(resp.find("\"state\":\"pending\""), std::string::npos) << resp;
  server_.stop();
}
