#pragma once
#include "common.h"

namespace bridge {
std::wstring user_profile_path();
bool save_credentials(const Profile& profile, const std::wstring& username, const std::wstring& password, DWORD& error);
bool read_credentials(const Profile& profile, std::wstring& username, std::wstring& password);
bool forget_credentials(const Profile& profile, DWORD& error);
// A public-key pin is trusted only for the canonical HTTPS host and port.
bool read_server_pin(const std::wstring& server, std::string& pin, DWORD& error, const std::wstring& directory = {});
bool save_server_pin(const std::wstring& server, const std::string& pin, DWORD& error, const std::wstring& directory = {});
bool select_server_profile(const Profile& configured, const std::wstring& server, Profile& selected, DWORD& error, const std::wstring& directory = {});
// Imports a .bvpn connection file or a CA certificate into this user's app-private store.
bool import_connection(const std::wstring& file, const Profile& current, std::wstring& error, const std::wstring& destination = {});
// Exports a portable connection profile containing public trust information only.
bool export_connection(const std::wstring& file, const Profile& profile, std::wstring& error);
}
