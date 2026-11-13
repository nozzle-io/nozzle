#include <cstdio>
#include <dlfcn.h>

int main(int argc, char **argv) {
    if(argc != 3 && argc != 5) {
        std::fprintf(stderr, "usage: plugin_host DSO ENTRY [DSO ENTRY]\n");
        return 2;
    }

    using probe_function = int (*)();
    void *handles[2]{};
    probe_function probes[2]{};
    const int count{(argc - 1) / 2};
    // Load both before calling either; reverse order runs in a fresh process.
    for(int i = 0; i < count; ++i) {
        const char *path{argv[1 + i * 2]};
        const char *entry{argv[2 + i * 2]};
        handles[i] = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
        if(!handles[i]) {
            std::fprintf(stderr, "dlopen %s: %s\n", path, dlerror());
            return 2;
        }
        dlerror();
        probes[i] = reinterpret_cast<probe_function>(dlsym(handles[i], entry));
        const char *error{dlerror()};
        if(error || !probes[i]) {
            std::fprintf(stderr, "dlsym %s: %s\n", entry, error ? error : "null entry");
            return 2;
        }
        std::printf("LOADED index=%d path=%s flags=RTLD_NOW|RTLD_GLOBAL\n", i, path);
    }

    bool passed{true};
    for(int i = 0; i < count; ++i) {
        passed &= probes[i]() == 0;
    }
    for(int i = count; 0 < i; --i) {
        if(dlclose(handles[i - 1]) != 0) {
            std::fprintf(stderr, "dlclose: %s\n", dlerror());
            return 2;
        }
    }
    std::printf("PLUGIN_ISOLATION count=%d result=%s\n", count, passed ? "PASS" : "FAIL");
    return passed ? 0 : 1;
}
