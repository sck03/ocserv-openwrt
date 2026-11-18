#pragma once
#include <NetworkManager.h>
#include <string>

namespace lansyra {
NMConnection *profile_candidate(NMConnection *existing, const std::string &name,
    const std::string &server, const std::string &username, const std::string &ca);
struct ImportedProfile {
    std::string name, server, ca;
    bool retained = false;
    ~ImportedProfile();
};
ImportedProfile *read_public_profile(const std::string &path);
void prune_profile_certificates(const GPtrArray *connections, const std::string &removedID);
}
