#pragma once
#include "common.h"

namespace bridge {
std::wstring user_profile_path();
bool save_credentials(const Profile& profile, const std::wstring& username, const std::wstring& password, DWORD& error);
bool read_credentials(const Profile& profile, std::wstring& username, std::wstring& password);
bool forget_credentials(const Profile& profile, DWORD& error);
// Imports a .bvpn connection file or a CA certificate into this user's app-private store.
bool import_connection(const std::wstring& file, const Profile& current, std::wstring& error, const std::wstring& destination = {});
}
