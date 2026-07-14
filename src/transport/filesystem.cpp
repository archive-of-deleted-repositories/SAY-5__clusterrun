#include "transport/filesystem.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace clusterrun {

namespace fs = std::filesystem;

namespace {

// Rejects path pieces that could escape the store root.
bool safe_piece(const std::string& piece) {
  if (piece.empty() || piece.front() == '/') {
    return false;
  }
  std::size_t start = 0;
  while (start <= piece.size()) {
    std::size_t slash = piece.find('/', start);
    std::string part = piece.substr(start, slash - start);
    if (part == "..") {
      return false;
    }
    if (slash == std::string::npos) {
      break;
    }
    start = slash + 1;
  }
  return true;
}

}  // namespace

FilesystemObjectStore::FilesystemObjectStore(std::string root) : root_(std::move(root)) {
  if (root_.empty()) {
    const char* env = std::getenv("CLUSTERRUN_FS_ROOT");
    root_ = env != nullptr ? env : "/tmp/clusterrun-objects";
  }
}

bool FilesystemObjectStore::ensure_bucket(const std::string& bucket) {
  if (!safe_piece(bucket)) {
    return false;
  }
  std::error_code ec;
  fs::create_directories(fs::path(root_) / bucket, ec);
  return !ec;
}

bool FilesystemObjectStore::put(const std::string& bucket, const std::string& key,
                                const std::string& body) {
  if (!safe_piece(bucket) || !safe_piece(key)) {
    return false;
  }
  fs::path path = fs::path(root_) / bucket / key;
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  if (ec) {
    return false;
  }
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out.good()) {
    return false;
  }
  out.write(body.data(), static_cast<std::streamsize>(body.size()));
  return out.good();
}

std::optional<std::string> FilesystemObjectStore::get(const std::string& bucket,
                                                      const std::string& key) {
  if (!safe_piece(bucket) || !safe_piece(key)) {
    return std::nullopt;
  }
  std::ifstream in(fs::path(root_) / bucket / key, std::ios::binary);
  if (!in.good()) {
    return std::nullopt;
  }
  std::ostringstream body;
  body << in.rdbuf();
  return body.str();
}

}  // namespace clusterrun
