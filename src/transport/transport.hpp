#pragma once

#include <optional>
#include <string>
#include <vector>

namespace clusterrun {

// A message pulled from a queue, carrying an opaque receipt used to delete it.
struct QueueMessage {
  std::string body;
  std::string receipt;
};

// An at-least-once work queue, shaped like Amazon SQS. The default build backs
// this with LocalStack over its REST API; a real deployment can back it with
// the AWS SDK behind the same interface.
class Queue {
 public:
  virtual ~Queue() = default;

  // Ensures the named queue exists and returns its URL. Idempotent.
  virtual std::string ensure_queue(const std::string& name) = 0;

  // Sends a message body to the queue identified by url.
  virtual bool send(const std::string& url, const std::string& body) = 0;

  // Receives up to max_messages, waiting up to wait_seconds for one to arrive.
  // Returns an empty vector when the queue is empty.
  virtual std::vector<QueueMessage> receive(const std::string& url, int max_messages,
                                            int wait_seconds) = 0;

  // Deletes a received message so it is not redelivered.
  virtual bool remove(const std::string& url, const std::string& receipt) = 0;
};

// A blob store shaped like Amazon S3.
class ObjectStore {
 public:
  virtual ~ObjectStore() = default;

  // Ensures the named bucket exists. Idempotent.
  virtual bool ensure_bucket(const std::string& bucket) = 0;

  // Writes body to bucket/key, overwriting any existing object.
  virtual bool put(const std::string& bucket, const std::string& key, const std::string& body) = 0;

  // Reads bucket/key, or nullopt if it does not exist.
  virtual std::optional<std::string> get(const std::string& bucket, const std::string& key) = 0;
};

}  // namespace clusterrun
