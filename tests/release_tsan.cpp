// BUG-0077: the setup's release state is written by the check thread and the install job and read
// by the window. Built with ThreadSanitizer (make test-tsan): two writers and a reader on
// upd::ReleaseBox. `release_tsan --old` uses the box without its lock (the old plain global):
// ThreadSanitizer then reports a data race and the run fails.
#include <cstdio>
#include <string>
#include <thread>

#include "../updater/pack.h"

int main(int argc, char** argv)
{
    if (argc > 1 && std::string(argv[1]) == "--old") {
        upd::g_release_lock_fix = false;
        printf("release lock OFF\n");
    }
    upd::ReleaseBox box;
    auto writer = [&](const char* v) {
        for (int i = 0; i < 20000; i++) {
            upd::ReleaseInfo ri;
            ri.ok = ri.done = true;
            ri.rel.version = v;
            ri.rel.pack_url = std::string("https://github.com/WoodyWizard/SporeCoop/releases/download/v") + v +
                              "/SporeCoop-" + v + ".pack";
            box.set(ri);
        }
    };
    size_t seen = 0;
    std::thread a(writer, "0.8.0.1234567890123456"), b(writer, "0.9.0.6543210987654321");
    std::thread r([&] {
        for (int i = 0; i < 20000; i++)
            seen += box.get().rel.version.size();
    });
    a.join();
    b.join();
    r.join();
    upd::ReleaseInfo last = box.get();
    bool ok = last.ok && (last.rel.version == "0.8.0.1234567890123456" || last.rel.version == "0.9.0.6543210987654321");
    printf("%s (read %zu chars)\n", ok ? "ALL OK" : "FAILED", seen);
    return ok ? 0 : 1;
}
