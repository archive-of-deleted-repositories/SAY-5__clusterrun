#pragma once

#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "transport/transport.hpp"

namespace clusterrun {

// In-memory ObjectStore: thread-safe so several worker threads can share one.
// Used by tests and benchmarks to exercise the system without LocalStack.
class MemoryObjectStore : public ObjectStore {
 public:
  bool ensure_bucket(const std::string& bucket) override {
    std::lock_guard<std::mutex> lk(m_);
    buckets_.insert(bucket);
    return true;
  }
  bool put(const std::string& bucket, const std::string& key, const std::string& body) override {
    std::lock_guard<std::mutex> lk(m_);
    blobs_[bucket + "/" + key] = body;
    return true;
  }
  std::optional<std::string> get(const std::string& bucket, const std::string& key) override {
    std::lock_guard<std::mutex> lk(m_);
    auto it = blobs_.find(bucket + "/" + key);
    if (it == blobs_.end()) {
      return std::nullopt;
    }
    return it->second;
  }
  std::size_t object_count() {
    std::lock_guard<std::mutex> lk(m_);
    return blobs_.size();
  }

 private:
  std::mutex m_;
  std::set<std::string> buckets_;
  std::map<std::string, std::string> blobs_;
};

// In-memory queue: at-least-once with explicit delete (receive pops here).
class MemoryQueue : public Queue {
 public:
  std::string ensure_queue(const std::string& name) override {
    std::lock_guard<std::mutex> lk(m_);
    queues_.try_emplace(name);
    return name;
  }
  bool send(const std::string& url, const std::string& body) override {
    std::lock_guard<std::mutex> lk(m_);
    queues_[url].push_back(body);
    return true;
  }
  std::vector<QueueMessage> receive(const std::string& url, int max_messages, int) override {
    std::lock_guard<std::mutex> lk(m_);
    std::vector<QueueMessage> out;
    auto& q = queues_[url];
    while (!q.empty() && static_cast<int>(out.size()) < max_messages) {
      QueueMessage msg;
      msg.body = q.front();
      msg.receipt = "r";
      q.pop_front();
      out.push_back(std::move(msg));
    }
    return out;
  }
  bool remove(const std::string&, const std::string&) override {
    return true;
  }

 private:
  std::mutex m_;
  std::map<std::string, std::deque<std::string>> queues_;
};

}  // namespace clusterrun
