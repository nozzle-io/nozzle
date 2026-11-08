#include "registry.hpp"

#include <cstdio>
#include <poll.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    auto registered = nozzle::detail::registry::register_sender(
        argv[1], "stale_cleanup_tests", 1, 4, 3, 0, 1);
    if (!registered.ok()) return 3;
    const auto &identity = registered.value();
    const bool announced = std::printf("%s %s\n", identity.uuid, identity.shm_name) > 0
        && std::fflush(stdout) == 0;
    // The parent keeps stdin open. EOF or a lost parent also bounds this owner.
    pollfd input{STDIN_FILENO, POLLIN, 0};
    const int ready = announced ? ::poll(&input, 1, 30000) : -1;
    auto removed = nozzle::detail::registry::unregister_sender(identity.uuid);
    return announced && ready >= 0 && removed.ok() ? 0 : 4;
}
