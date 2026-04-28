// nozzle - metadata.cpp - Metadata serialization and parsing

#include "metadata.hpp"

#include <cstring>
#include <string>

namespace bbb {
namespace nozzle {
namespace detail {

metadata_list parse_metadata(const char *buf, std::size_t buf_size) {
	metadata_list result;
	if (!buf || buf_size == 0) {
		return result;
	}

	std::size_t pos = 0;
	while (pos < buf_size) {
		if (buf[pos] == '\0') {
			break;
		}

		const char *entry = buf + pos;
		auto entry_len = std::strlen(entry);
		if (entry_len == 0) {
			break;
		}

		const char *eq = std::strchr(entry, '=');
		if (eq && eq != entry) {
			std::string key(entry, static_cast<std::size_t>(eq - entry));
			std::string value(eq + 1);
			result.push_back({std::move(key), std::move(value)});
		}

		pos += entry_len + 1;
	}

	return result;
}

std::size_t serialize_metadata(const metadata_list &md, char *buf, std::size_t buf_size) {
	if (!buf || buf_size == 0) {
		return 0;
	}

	std::memset(buf, 0, buf_size);
	std::size_t offset = 0;

	for (const auto &kv : md) {
		std::size_t entry_len = kv.key.size() + 1 + kv.value.size() + 1;
		if (offset + entry_len >= buf_size) {
			break;
		}

		std::memcpy(buf + offset, kv.key.c_str(), kv.key.size());
		offset += kv.key.size();
		buf[offset++] = '=';
		std::memcpy(buf + offset, kv.value.c_str(), kv.value.size());
		offset += kv.value.size();
		buf[offset++] = '\0';
	}

	return offset;
}

} // namespace detail
} // namespace nozzle
} // namespace bbb
