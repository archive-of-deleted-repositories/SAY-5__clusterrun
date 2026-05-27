#pragma once

#include "transport/memory.hpp"

namespace clusterrun {

// Tests historically used these names; they alias the shared in-memory
// transports so test and bench code share one implementation.
using FakeObjectStore = MemoryObjectStore;
using FakeQueue = MemoryQueue;

}  // namespace clusterrun
