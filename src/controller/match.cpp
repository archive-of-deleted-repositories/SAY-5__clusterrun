#include "controller/match.hpp"

#include <algorithm>

namespace clusterrun {

bool capability_matches(const Requirements& reqs, const Capability& cap) {
  if (cap.cpu < reqs.cpu_min) {
    return false;
  }
  if (cap.mem_gb < reqs.mem_gb_min) {
    return false;
  }
  for (const auto& want : reqs.features) {
    if (std::find(cap.features.begin(), cap.features.end(), want) == cap.features.end()) {
      return false;
    }
  }
  if (!reqs.node_class.empty() && reqs.node_class != cap.node_class) {
    return false;
  }
  return true;
}

}  // namespace clusterrun
