#pragma once

#include <cstdint>

#include <nozzle/format_resolve.hpp>
#include <nozzle/types.hpp>

namespace nozzle {
namespace detail {

struct single_step_texture_attempt_plan {
    texture_format primary{texture_format::unknown};
    texture_format fallback{texture_format::unknown};
    fallback_category fallback_cat{fallback_category::none};
    bool has_fallback{false};
};

inline single_step_texture_attempt_plan make_single_step_texture_attempt_plan(
    texture_format requested, uint32_t fallback_flags) {
    single_step_texture_attempt_plan plan;
    plan.primary = requested;
    auto fb = resolve_fallback(requested, fallback_flags);
    if (fb.valid) {
        plan.fallback = fb.target;
        plan.fallback_cat = fb.category;
        plan.has_fallback = true;
    }
    return plan;
}

} // namespace detail
} // namespace nozzle
