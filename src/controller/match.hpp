#pragma once

#include "core/types.hpp"

namespace clusterrun {

// Returns true if a worker advertising cap can run a job declaring reqs:
//   - cap.cpu >= reqs.cpu_min
//   - cap.mem_gb >= reqs.mem_gb_min
//   - cap advertises every feature in reqs.features
//   - reqs.node_class is empty, or equals cap.node_class
bool capability_matches(const Requirements& reqs, const Capability& cap);

}  // namespace clusterrun
