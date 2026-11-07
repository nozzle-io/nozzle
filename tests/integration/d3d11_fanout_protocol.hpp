#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fanout_test {

using clock = std::chrono::steady_clock;
constexpr uint32_t width = 4;
constexpr uint32_t height = 3;

inline double milliseconds(clock::time_point start) {
    return std::chrono::duration<double, std::milli>(clock::now() - start).count();
}

// One writer per file. Rename publishes a complete record; sequence numbers
// prevent an earlier reply from satisfying a later command.
inline bool publish_file(const std::filesystem::path &path, const std::string &text) {
    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream stream(temporary);
        stream << text << '\n';
        stream.close();
        if (!stream) return false;
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    return !error;
}

inline bool read_file(const std::filesystem::path &path, std::string &text) {
    std::ifstream stream(path);
    return static_cast<bool>(std::getline(stream, text));
}

// Wire order: code acquire_code commit_code wrote pixels_ok frame resource
//            acquire_ms commit_ms total_ms diagnostic. -1 means not attempted;
// code -2 means the actor refused to write a forbidden texture after acquisition.
struct reply {
    int code{-1};
    int acquire_code{-1};
    int commit_code{-1};
    bool wrote{false};
    bool pixels_ok{false};
    uint64_t frame{0};
    uint64_t resource{0};
    double acquire_ms{0};
    double commit_ms{0};
    double total_ms{0};
    std::string diagnostic{"none"};

    std::string encode() const {
        std::ostringstream out;
        out << code << ' ' << acquire_code << ' ' << commit_code << ' '
            << wrote << ' ' << pixels_ok << ' ' << frame << ' ' << resource
            << ' ' << acquire_ms << ' ' << commit_ms << ' ' << total_ms << ' ' << diagnostic;
        return out.str();
    }

    bool decode(const std::string &text) {
        std::istringstream in(text);
        return static_cast<bool>(in >> code >> acquire_code >> commit_code
            >> wrote >> pixels_ok >> frame >> resource >> acquire_ms >> commit_ms >> total_ms >> diagnostic);
    }
};

inline uint8_t channel(uint32_t seed, uint32_t x, uint32_t y, uint32_t c) {
    // Distinct channels, corners, alpha, rows, columns, and publication seeds.
    return static_cast<uint8_t>((seed * 29 + x * 37 + y * 61 + c * 47 + 13) & 255);
}

} // namespace fanout_test
