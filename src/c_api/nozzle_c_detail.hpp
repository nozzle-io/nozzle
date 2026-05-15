#pragma once

#include <nozzle/nozzle_c.h>
#include <nozzle/types.hpp>

NozzleErrorCode nozzle_c_detail_fill_fallback(
    NozzleFormatFallbackInfo *out_info,
    const nozzle::format_fallback_info &fb
);
