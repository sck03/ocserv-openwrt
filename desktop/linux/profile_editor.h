#pragma once
#include "profile_settings.h"
#include <gtk/gtk.h>
#include <optional>

namespace lansyra {
struct ProfileDraft {
    std::string name, server, username, ca;
};
std::optional<ProfileDraft> edit_profile(GtkWindow *owner, NMConnection *existing,
                                         const ImportedProfile *imported);
} // namespace lansyra
