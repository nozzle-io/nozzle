#pragma once

// Compiler-detected feature flags for nozzle internals.
// These are set based on the actual compiler state, not user configuration.

#if defined(__clang__)
    #if __has_feature(cxx_exceptions)
        #define NOZZLE_HAS_EXCEPTIONS 1
    #else
        #define NOZZLE_HAS_EXCEPTIONS 0
    #endif
#elif defined(_MSC_VER)
    #if defined(_CPPUNWIND)
        #define NOZZLE_HAS_EXCEPTIONS 1
    #else
        #define NOZZLE_HAS_EXCEPTIONS 0
    #endif
#elif defined(__GNUC__)
    #if defined(__EXCEPTIONS)
        #define NOZZLE_HAS_EXCEPTIONS 1
    #else
        #define NOZZLE_HAS_EXCEPTIONS 0
    #endif
#else
    #define NOZZLE_HAS_EXCEPTIONS 0
#endif
