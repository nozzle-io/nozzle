#pragma once

// Compiler-detected feature flags for nozzle internals.
// These are set based on the actual compiler state, not user configuration.

#if defined(_MSC_VER)
    #if _HAS_EXCEPTIONS
        #define NOZZLE_HAS_EXCEPTIONS 1
    #endif
#elif defined(__clang__)
    #if __has_feature(cxx_exceptions)
        #define NOZZLE_HAS_EXCEPTIONS 1
    #endif
#elif defined(__GNUC__)
    #if defined(__EXCEPTIONS)
        #define NOZZLE_HAS_EXCEPTIONS 1
    #endif
#endif
