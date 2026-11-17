#include "preferences.h"
#include <glib/gstdio.h>
#include <cassert>
#include <iostream>

int main() {
    g_autofree char *directory = g_dir_make_tmp("lansyra-preferences-XXXXXX", nullptr);
    assert(directory);
    unsigned completed = 0;
    bool failed = false;
    {
        lansyra::PreferencesRepository repository(directory);
        lansyra::Preferences value;
        assert(repository.load(value) && value.automatic.empty());
        for (unsigned i = 0; i != 100; ++i)
            repository.save({std::to_string(i), "profile", i == 99}, [&](bool ok) {
                failed |= !ok;
                ++completed;
            });
        assert(completed == 0); // No disk work or callbacks on the caller's stack.
        while (completed != 100) g_main_context_iteration(nullptr, TRUE);
        assert(!failed && repository.load(value) && value.selected == "99" && value.retry);
        const auto startup = std::string(directory) + "/autostart/io.github.sck03.lansyra.desktop";
        assert(g_file_test(startup.c_str(), G_FILE_TEST_EXISTS));
        repository.save({"99", "", false}, [&](bool ok) { failed |= !ok; ++completed; });
        while (completed != 101) g_main_context_iteration(nullptr, TRUE);
        assert(!failed && !g_file_test(startup.c_str(), G_FILE_TEST_EXISTS));
        g_unlink((std::string(directory) + "/lansyra/settings.ini").c_str());
        g_rmdir((std::string(directory) + "/lansyra").c_str());
        g_rmdir((std::string(directory) + "/autostart").c_str());
    }
    // A regular file in place of the config directory forces a real write failure.
    const auto blocked = std::string(directory) + "/blocked";
    assert(g_file_set_contents(blocked.c_str(), "fixture", -1, nullptr));
    {
        lansyra::PreferencesRepository repository(blocked);
        repository.save({"id", "id", false}, [&](bool ok) { assert(!ok); ++completed; });
        while (completed != 102) g_main_context_iteration(nullptr, TRUE);
    }
    g_unlink(blocked.c_str());
    g_rmdir(directory);
    std::cout << "102 async preference completions, coalescing, autostart and failure checks passed\n";
}
