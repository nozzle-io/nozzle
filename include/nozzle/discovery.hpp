#pragma once

#include <nozzle/types.hpp>

#include <vector>

namespace nozzle {

// Discover available senders on the local machine
std::vector<sender_info> enumerate_senders();

} // namespace nozzle
