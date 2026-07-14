#include "controller/control_server.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <nlohmann/json.hpp>

#include "controller/retry_classifier.hpp"
#include "core/job_codec.hpp"
#include "core/version.hpp"

namespace clusterrun {

namespace {

using nlohmann::json;

const char* reason_phrase(int status) {
  switch (status) {
    case 200:
      return "OK";
    case 400:
      return "Bad Request";
    case 404:
      return "Not Found";
    case 405:
      return "Method Not Allowed";
    default:
      return "Internal Server Error";
  }
}

std::string frame(const ControlResponse& r) {
  std::string out =
      "HTTP/1.1 " + std::to_string(r.status) + " " + reason_phrase(r.status) +
      "\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(r.body.size()) +
      "\r\nConnection: close\r\n\r\n" + r.body;
  return out;
}

ControlResponse err(int status, const std::string& message) {
  json j;
  j["error"] = message;
  return {status, j.dump()};
}

// Human-readable lifecycle state for a job, derived from scheduler bookkeeping.
std::string job_state_name(const Scheduler& sched, JobId id, const JobState& st) {
  if (st.last_status == Status::kSucceeded) {
    return "succeeded";
  }
  for (JobId dead : sched.dead_letters()) {
    if (dead == id) {
      return "dead_lettered";
    }
  }
  if (st.assigned_to.has_value()) {
    switch (st.last_status) {
      case Status::kRunning:
        return "running";
      case Status::kCheckpointed:
        return "checkpointed";
      default:
        return "claimed";
    }
  }
  auto waiting = sched.waiting_for();
  if (waiting.count(id) > 0) {
    return "waiting";
  }
  return "pending";
}

// Parses the trailing integer of "/v1/jobs/<id>". Returns false on garbage.
bool parse_job_id(const std::string& path, std::size_t prefix_len, JobId* out) {
  std::string tail = path.substr(prefix_len);
  if (tail.empty() || tail.find_first_not_of("0123456789") != std::string::npos) {
    return false;
  }
  *out = std::strtoull(tail.c_str(), nullptr, 10);
  return true;
}

}  // namespace

ControlServer::ControlServer(Controller& ctrl, std::mutex& mu, std::uint16_t port)
    : ctrl_(ctrl), mu_(mu), port_(port) {
}

ControlServer::~ControlServer() {
  stop();
}

ControlResponse ControlServer::route(const std::string& method, const std::string& path,
                                     const std::string& body) {
  std::lock_guard<std::mutex> lk(mu_);

  if (path == "/v1/healthz" && method == "GET") {
    json j;
    j["status"] = "ok";
    j["version"] = version();
    return {200, j.dump()};
  }

  if (path == "/v1/jobs" && method == "POST") {
    auto job = decode_job(body);
    if (!job) {
      return err(400, "invalid jobspec");
    }
    if (ctrl_.scheduler().job_state(job->job_id) != nullptr) {
      return err(400, "job_id already known");
    }
    ctrl_.submit(*job);
    json j;
    j["job_id"] = job->job_id;
    j["state"] = "pending";
    return {200, j.dump()};
  }

  const std::string jobs_prefix = "/v1/jobs/";
  if (path.rfind(jobs_prefix, 0) == 0 && method == "GET") {
    JobId id = 0;
    if (!parse_job_id(path, jobs_prefix.size(), &id)) {
      return err(400, "bad job id");
    }
    const JobState* st = ctrl_.scheduler().job_state(id);
    if (st == nullptr) {
      return err(404, "unknown job");
    }
    json j;
    j["job_id"] = id;
    j["state"] = job_state_name(ctrl_.scheduler(), id, *st);
    j["attempts"] = st->attempts;
    j["attempts_max"] = st->job.attempts_max;
    j["priority"] = to_string(st->job.priority);
    if (!st->job.tenant.empty()) {
      j["tenant"] = st->job.tenant;
    }
    if (st->assigned_to.has_value()) {
      j["worker_id"] = *st->assigned_to;
    }
    if (!st->last_detail.empty()) {
      j["detail"] = st->last_detail;
      j["category"] = to_string(parse_category(st->last_detail));
    }
    if (!st->latest_checkpoint_s3.empty()) {
      j["checkpoint"] = st->latest_checkpoint_s3;
    }
    return {200, j.dump()};
  }

  if (path == "/v1/queue" && method == "GET") {
    const Scheduler& sched = ctrl_.scheduler();
    json j;
    j["pending"] = sched.pending_count();
    j["pending_by_priority"] = {
        {"high", sched.pending_count(Priority::kHigh)},
        {"normal", sched.pending_count(Priority::kNormal)},
        {"low", sched.pending_count(Priority::kLow)},
    };
    j["in_flight"] = sched.in_flight_count();
    j["done"] = sched.done_count();
    j["dead_lettered"] = sched.dlq_count();
    json waiting = json::object();
    for (const auto& [id, reason] : sched.waiting_for()) {
      waiting[std::to_string(id)] = reason;
    }
    j["waiting"] = waiting;
    return {200, j.dump()};
  }

  return err(404, "no such route");
}

bool ControlServer::start() {
  listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd_ < 0) {
    return false;
  }
  int one = 1;
  ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port_);
  if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
      ::listen(listen_fd_, 16) != 0) {
    ::close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }
  socklen_t len = sizeof(addr);
  if (::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len) == 0) {
    port_ = ntohs(addr.sin_port);
  }

  running_ = true;
  thread_ = std::thread([this] { serve(); });
  return true;
}

void ControlServer::stop() {
  if (!running_.exchange(false)) {
    return;
  }
  ::shutdown(listen_fd_, SHUT_RDWR);
  ::close(listen_fd_);
  listen_fd_ = -1;
  if (thread_.joinable()) {
    thread_.join();
  }
}

void ControlServer::serve() {
  while (running_) {
    int fd = ::accept(listen_fd_, nullptr, nullptr);
    if (fd < 0) {
      if (!running_) {
        break;
      }
      continue;
    }
    handle_connection(fd);
    ::close(fd);
  }
}

void ControlServer::handle_connection(int fd) {
  timeval tv{};
  tv.tv_sec = 5;
  ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  // Read until the header terminator, then until content-length is satisfied.
  std::string raw;
  char buf[4096];
  std::size_t header_end = std::string::npos;
  while (header_end == std::string::npos) {
    ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
    if (n <= 0) {
      return;
    }
    raw.append(buf, static_cast<std::size_t>(n));
    header_end = raw.find("\r\n\r\n");
    if (raw.size() > 1 << 20) {
      return;  // oversized header block
    }
  }

  std::string head = raw.substr(0, header_end);
  std::string body = raw.substr(header_end + 4);

  std::size_t sp1 = head.find(' ');
  std::size_t sp2 = head.find(' ', sp1 + 1);
  if (sp1 == std::string::npos || sp2 == std::string::npos) {
    return;
  }
  std::string method = head.substr(0, sp1);
  std::string path = head.substr(sp1 + 1, sp2 - sp1 - 1);

  std::size_t content_length = 0;
  std::string lowered = head;
  for (char& c : lowered) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  std::size_t cl = lowered.find("content-length:");
  if (cl != std::string::npos) {
    content_length = std::strtoull(head.c_str() + cl + 15, nullptr, 10);
    if (content_length > 4u << 20) {
      std::string resp = frame(err(400, "body too large"));
      ::send(fd, resp.data(), resp.size(), 0);
      return;
    }
  }
  while (body.size() < content_length) {
    ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
    if (n <= 0) {
      return;
    }
    body.append(buf, static_cast<std::size_t>(n));
  }
  body.resize(content_length);

  std::string resp = frame(route(method, path, body));
  ::send(fd, resp.data(), resp.size(), 0);
}

}  // namespace clusterrun
