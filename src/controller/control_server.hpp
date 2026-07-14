#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include "controller/controller.hpp"
#include "transport/transport.hpp"

namespace clusterrun {

// A routed control-plane response before HTTP framing.
struct ControlResponse {
  int status = 200;
  std::string body;
  std::string content_type = "application/json";
  std::string checksum;  // sha256 hex, sent as X-Checksum-Sha256 when set
};

// A minimal HTTP/1.1 control endpoint for the controller, mirroring its
// internal submit/status API so external clients (the crun CLI) can drive it
// without touching the SQS transport. Routing logic is separated from the
// socket loop so it can be unit tested directly; see PROTOCOL.md for the wire
// contract.
//
// The server serializes every request against the supplied mutex, which the
// controller event loop holds while it steps, so Controller needs no internal
// locking of its own.
class ControlServer {
 public:
  // port 0 asks the kernel for an ephemeral port; port() reports the choice.
  // store, when given, backs GET /v1/jobs/{id}/result artifact downloads.
  ControlServer(Controller& ctrl, std::mutex& mu, std::uint16_t port, ObjectStore* store = nullptr);
  ~ControlServer();

  ControlServer(const ControlServer&) = delete;
  ControlServer& operator=(const ControlServer&) = delete;

  // Binds, listens on 0.0.0.0, and starts the accept thread. Returns false if
  // the socket could not be bound.
  bool start();
  void stop();

  std::uint16_t port() const {
    return port_;
  }

  // Dispatches one request to its handler under the controller mutex. Public
  // so tests can exercise routing without sockets.
  ControlResponse route(const std::string& method, const std::string& path,
                        const std::string& body);

 private:
  void serve();
  void handle_connection(int fd);

  Controller& ctrl_;
  std::mutex& mu_;
  ObjectStore* store_;
  std::uint16_t port_;
  int listen_fd_ = -1;
  std::atomic<bool> running_{false};
  std::thread thread_;
};

}  // namespace clusterrun
