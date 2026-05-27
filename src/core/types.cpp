#include "core/types.hpp"

namespace clusterrun {

const char* to_string(Status s) {
  switch (s) {
    case Status::kClaimed:
      return "claimed";
    case Status::kRunning:
      return "running";
    case Status::kCheckpointed:
      return "checkpointed";
    case Status::kSucceeded:
      return "succeeded";
    case Status::kFailed:
      return "failed";
    case Status::kLost:
      return "lost";
  }
  return "unknown";
}

}  // namespace clusterrun
