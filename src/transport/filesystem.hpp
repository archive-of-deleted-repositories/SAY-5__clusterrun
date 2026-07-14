#pragma once

#include <string>

#include "transport/transport.hpp"

namespace clusterrun {

// ObjectStore backed by a local directory tree: root/bucket/key. This is the
// object store for the local backend, where the controller and workers share
// a filesystem instead of S3. Keys may contain slashes; parent directories
// are created on demand. Keys with ".." components are rejected.
class FilesystemObjectStore : public ObjectStore {
 public:
  // Uses CLUSTERRUN_FS_ROOT when root is empty, and a default under /tmp when
  // neither is set.
  explicit FilesystemObjectStore(std::string root = "");

  bool ensure_bucket(const std::string& bucket) override;
  bool put(const std::string& bucket, const std::string& key, const std::string& body) override;
  std::optional<std::string> get(const std::string& bucket, const std::string& key) override;

  const std::string& root() const {
    return root_;
  }

 private:
  std::string root_;
};

}  // namespace clusterrun
