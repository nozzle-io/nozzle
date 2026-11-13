#include <nozzle/backend_capabilities.hpp>
#include <nozzle/nozzle_c.h>

#include <cstdio>
#include <dlfcn.h>

extern "C" __attribute__((visibility("default"))) int NOZZLE_PROBE_NAME();

namespace {

bool check_owner(const char *name, const void *address, const Dl_info &plugin) {
    Dl_info owner{};
    const bool found{dladdr(address, &owner) != 0};
    const bool matches{found && owner.dli_fbase == plugin.dli_fbase};
    std::printf("SYMBOL_OWNER revision=%s symbol=%s plugin=%s owner=%s result=%s\n",
        NOZZLE_CORE_REVISION, name, plugin.dli_fname,
        found ? owner.dli_fname : "MISSING", matches ? "PASS" : "FAIL");
    return matches;
}

} // namespace

extern "C" __attribute__((visibility("default"))) int NOZZLE_PROBE_NAME() {
    Dl_info plugin{};
    if(dladdr(reinterpret_cast<const void *>(&NOZZLE_PROBE_NAME), &plugin) == 0) {
        std::fprintf(stderr, "cannot locate plugin entry point\n");
        return 1;
    }

    bool passed{true};
    passed &= check_owner("nozzle_backend_is_available",
        reinterpret_cast<const void *>(&nozzle_backend_is_available), plugin);
    passed &= check_owner("nozzle::is_backend_available",
        reinterpret_cast<const void *>(&nozzle::is_backend_available), plugin);
    passed &= check_owner("nozzle_sender_create",
        reinterpret_cast<const void *>(&nozzle_sender_create), plugin);

    const int c_available{nozzle_backend_is_available(NOZZLE_BACKEND_OPENGL)};
    const int cpp_available{nozzle::is_backend_available(nozzle::backend_type::opengl) ? 1 : 0};
    const bool values_match{c_available == NOZZLE_EXPECT_OPENGL &&
                           cpp_available == NOZZLE_EXPECT_OPENGL};
    std::printf("BACKEND_CALL revision=%s expected=%d c=%d cpp=%d result=%s\n",
        NOZZLE_CORE_REVISION, NOZZLE_EXPECT_OPENGL, c_available, cpp_available,
        values_match ? "PASS" : "FAIL");
    passed &= values_match;

    NozzleSender *sender{nullptr};
    passed &= nozzle_sender_create(nullptr, &sender) == NOZZLE_ERROR_INVALID_ARGUMENT;
    passed &= sender == nullptr;
    return passed ? 0 : 1;
}
