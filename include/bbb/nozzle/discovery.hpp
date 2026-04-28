#pragma once

#include <bbb/nozzle/types.hpp>

#include <vector>

namespace bbb::nozzle {

// Discover available senders on the local machine
std::vector<sender_info> enumerate_senders();

} // namespace bbb::nozzle
