#pragma once

#include <string>

#include "transport/http_client.hpp"
#include "transport/transport.hpp"

namespace clusterrun {

// Returns the LocalStack endpoint, honoring AWS_ENDPOINT_URL and falling back to
// http://localhost:4566.
std::string localstack_endpoint();

// SQS-shaped queue backed by LocalStack's query (form-POST) API.
class LocalStackQueue : public Queue {
 public:
  explicit LocalStackQueue(std::string endpoint = localstack_endpoint());

  std::string ensure_queue(const std::string& name) override;
  bool send(const std::string& url, const std::string& body) override;
  std::vector<QueueMessage> receive(const std::string& url, int max_messages,
                                    int wait_seconds) override;
  bool remove(const std::string& url, const std::string& receipt) override;

 private:
  std::string endpoint_;
  HttpClient http_;
};

// S3-shaped object store backed by LocalStack's path-style REST API.
class LocalStackObjectStore : public ObjectStore {
 public:
  explicit LocalStackObjectStore(std::string endpoint = localstack_endpoint());

  bool ensure_bucket(const std::string& bucket) override;
  bool put(const std::string& bucket, const std::string& key, const std::string& body) override;
  std::optional<std::string> get(const std::string& bucket, const std::string& key) override;

 private:
  std::string endpoint_;
  HttpClient http_;
};

// Parses "s3://bucket/key/path" into its bucket and key. Returns false if the
// uri is not an s3 url with a non-empty key.
bool parse_s3_uri(const std::string& uri, std::string* bucket, std::string* key);

}  // namespace clusterrun
