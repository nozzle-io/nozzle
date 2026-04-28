#pragma once

#include <bbb/nozzle/types.hpp>

#include <cstddef>

namespace bbb {
namespace nozzle {
namespace detail {

metadata_list parse_metadata(const char *buf, std::size_t buf_size);
std::size_t serialize_metadata(const metadata_list &md, char *buf, std::size_t buf_size);

} // namespace detail
} // namespace nozzle
} // namespace bbb
