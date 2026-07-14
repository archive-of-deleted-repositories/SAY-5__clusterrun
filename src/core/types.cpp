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

const char* to_string(Priority p) {
  switch (p) {
    case Priority::kLow:
      return "low";
    case Priority::kNormal:
      return "normal";
    case Priority::kHigh:
      return "high";
  }
  return "normal";
}

bool parse_priority(const std::string& token, Priority* out) {
  if (token == "low") {
    *out = Priority::kLow;
    return true;
  }
  if (token == "normal") {
    *out = Priority::kNormal;
    return true;
  }
  if (token == "high") {
    *out = Priority::kHigh;
    return true;
  }
  return false;
}

}  // namespace clusterrun
