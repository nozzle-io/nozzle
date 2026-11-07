#pragma once

#include <string>
#include <vector>

namespace name_cases {

inline std::vector<std::string> invalid_utf8() {
    return {
        "\x80", "\xBF", "\xC0\xAF", "\xC1\xBF", "\xC2",
        "\xE6\xB8", "\xE0\x80\xAF", "\xED\xA0\x80", "\xED\xBF\xBF",
        "\xF0\x80\x80\xAF", "\xF4\x90\x80\x80", "\xF5\x80\x80\x80",
        "\xFF", "\xE6\x41\x80", "\xF0\x9F\x98",
        // JNI modified UTF-8: encoded NUL and a supplementary surrogate pair.
        "\xC0\x80", "\xED\xA0\xBD\xED\xB8\x80"
    };
}

inline std::vector<std::string> invalid_names() {
    auto names = invalid_utf8();
    names.emplace_back(64, 'n');
    names.push_back(std::string(63, 'n') + "LEFT");
    names.push_back(std::string(63, 'n') + "RIGHT");
    return names;
}

struct valid_case {
    std::string name;
    std::string application;
    std::string stored_application;
};

inline std::vector<valid_case> valid_names() {
    return {
        {std::string(63, 'n'), std::string(63, 'a'), std::string(63, 'a')},
        {std::string(61, 'n') + "\xC3\xA9", std::string(62, 'a') + "\xC3\xA9", std::string(62, 'a')},
        {std::string(60, 'n') + "\xE6\xB8\x8B", std::string(61, 'a') + "\xE6\xB8\x8B", std::string(61, 'a')},
        {std::string(59, 'n') + "\xF0\x9F\x98\x80", std::string(60, 'a') + "\xF0\x9F\x98\x80", std::string(60, 'a')},
        {"name_ascii_overflow", std::string(64, 'a'), std::string(63, 'a')},
        {"name_boundary_2", std::string(61, 'a') + "\xC3\xA9!", std::string(61, 'a') + "\xC3\xA9"},
        {"name_boundary_3", std::string(60, 'a') + "\xE6\xB8\x8B!", std::string(60, 'a') + "\xE6\xB8\x8B"},
        {"name_boundary_4", std::string(59, 'a') + "\xF0\x9F\x98\x80!", std::string(59, 'a') + "\xF0\x9F\x98\x80"},
        // Shortest/longest sequences in each range, including either side of surrogates.
        {"name_unicode_limits", "\x7F\xC2\x80\xDF\xBF\xE0\xA0\x80\xED\x9F\xBF\xEE\x80\x80\xEF\xBF\xBF\xF0\x90\x80\x80\xF4\x8F\xBF\xBF",
            "\x7F\xC2\x80\xDF\xBF\xE0\xA0\x80\xED\x9F\xBF\xEE\x80\x80\xEF\xBF\xBF\xF0\x90\x80\x80\xF4\x8F\xBF\xBF"}
    };
}

} // namespace name_cases
