#include "settings.h"
#include <wincred.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <algorithm>
#include <cstring>

namespace bridge {
std::wstring user_profile_path() {
    auto path = settings_path();
    return path.empty() ? L"" : path.substr(0, path.find_last_of(L'\\')) + L"\\connection.ini";
}
static bool read_file(const std::wstring& path, std::vector<BYTE>& data, DWORD& error) {
    Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file) { error = GetLastError(); return false; }
    LARGE_INTEGER length{};
    if (!GetFileSizeEx(file.get(), &length) || length.QuadPart < 1 || length.QuadPart > 1024*1024) { error = ERROR_INVALID_DATA; return false; }
    data.resize(static_cast<size_t>(length.QuadPart));
    DWORD got = 0;
    if (!ReadFile(file.get(), data.data(), static_cast<DWORD>(data.size()), &got, nullptr) || got != data.size()) { error = ERROR_READ_FAULT; return false; }
    return true;
}
static std::string sha256(const std::vector<BYTE>& bytes) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return {};
    DWORD size = 0, got = 0;
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<BYTE*>(&size), sizeof(size), &got, 0) < 0) { BCryptCloseAlgorithmProvider(algorithm,0); return {}; }
    std::vector<BYTE> object(size), digest(32);
    bool ok = BCryptCreateHash(algorithm, &hash, object.data(), size, nullptr, 0, 0) >= 0;
    if (ok) ok = BCryptHashData(hash, const_cast<BYTE*>(bytes.data()), static_cast<ULONG>(bytes.size()), 0) >= 0 && BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()),0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm,0);
    if (!ok) return {};
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    for (BYTE value : digest) { result.push_back(hex[value >> 4]); result.push_back(hex[value & 15]); }
    return result;
}
static std::wstring credential_target(const Profile& profile, DWORD& error) {
    std::wstring server;
    if (!normalize_server(profile.server, server)) { error = ERROR_INVALID_PARAMETER; return {}; }
    auto identity = utf8(server) + "\n" + profile.pin + "\n";
    std::vector<BYTE> bytes(identity.begin(), identity.end());
    if (!profile.ca_file.empty()) {
        std::vector<BYTE> ca;
        if (!read_file(profile.ca_file, ca, error)) return {};
        bytes.insert(bytes.end(), ca.begin(), ca.end());
    }
    auto hash = sha256(bytes);
    if (hash.empty()) { error = ERROR_GEN_FAILURE; return {}; }
    return L"BulijieVPN/" + wide(hash);
}
bool save_credentials(const Profile& profile, const std::wstring& username, const std::wstring& password, DWORD& error) {
    error = ERROR_SUCCESS;
    auto target = credential_target(profile, error);
    if (target.empty()) return false;
    auto secret = utf8(password);
    if (username.empty() || secret.empty() || secret.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE) { erase(secret); error = ERROR_BAD_LENGTH; return false; }
    CREDENTIALW entry{};
    entry.Type = CRED_TYPE_GENERIC;
    entry.TargetName = target.data();
    entry.UserName = const_cast<wchar_t*>(username.c_str());
    entry.Comment = const_cast<wchar_t*>(L"布利杰VPN");
    entry.CredentialBlobSize = static_cast<DWORD>(secret.size());
    entry.CredentialBlob = reinterpret_cast<BYTE*>(secret.data());
    entry.Persist = CRED_PERSIST_LOCAL_MACHINE; // Current Windows user, on this computer, across logins.
    bool ok = CredWriteW(&entry,0) != FALSE;
    if (!ok) error = GetLastError();
    erase(secret);
    return ok;
}
bool read_credentials(const Profile& profile, std::wstring& username, std::wstring& password) {
    DWORD error = 0;
    auto target = credential_target(profile,error);
    if (target.empty()) return false;
    PCREDENTIALW entry = nullptr;
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC,0,&entry)) return false;
    bool ok = entry->UserName && entry->CredentialBlob && entry->CredentialBlobSize > 0 && entry->CredentialBlobSize <= CRED_MAX_CREDENTIAL_BLOB_SIZE;
    if (ok) {
        username = entry->UserName;
        std::string secret(reinterpret_cast<char*>(entry->CredentialBlob), entry->CredentialBlobSize);
        password = wide(secret);
        erase(secret);
    }
    if (entry->CredentialBlob) SecureZeroMemory(entry->CredentialBlob, entry->CredentialBlobSize);
    CredFree(entry);
    return ok;
}
bool forget_credentials(const Profile& profile, DWORD& error) {
    error = 0;
    auto target = credential_target(profile,error);
    if (target.empty()) return false;
    if (CredDeleteW(target.c_str(),CRED_TYPE_GENERIC,0)) return true;
    error = GetLastError();
    return error == ERROR_NOT_FOUND;
}
static bool certificate_pem(const std::vector<BYTE>& data, std::string& pem) {
    std::string text(data.begin(), data.end());
    if (text.find("PRIVATE KEY") != std::string::npos) return false;
    std::vector<BYTE> binary;
    if (text.find("-----BEGIN CERTIFICATE-----") != std::string::npos) {
        auto second = text.find("-----BEGIN CERTIFICATE-----", text.find("-----BEGIN CERTIFICATE-----") + 1);
        if (second != std::string::npos) return false; // A connection file imports one explicitly chosen CA.
        DWORD size = 0;
        if (!CryptStringToBinaryA(text.c_str(),static_cast<DWORD>(text.size()),CRYPT_STRING_BASE64HEADER,nullptr,&size,nullptr,nullptr)) return false;
        binary.resize(size);
        if (!CryptStringToBinaryA(text.c_str(),static_cast<DWORD>(text.size()),CRYPT_STRING_BASE64HEADER,binary.data(),&size,nullptr,nullptr)) return false;
    } else binary = data;
    PCCERT_CONTEXT cert = CertCreateCertificateContext(X509_ASN_ENCODING,binary.data(),static_cast<DWORD>(binary.size()));
    if (!cert) return false;
    bool valid = CertVerifyTimeValidity(nullptr, cert->pCertInfo) == 0;
    auto* extension = CertFindExtension(szOID_BASIC_CONSTRAINTS2,cert->pCertInfo->cExtension,cert->pCertInfo->rgExtension);
    CERT_BASIC_CONSTRAINTS2_INFO* basic = nullptr;
    DWORD basic_size = 0;
    valid = valid && extension && CryptDecodeObjectEx(X509_ASN_ENCODING,X509_BASIC_CONSTRAINTS2,extension->Value.pbData,extension->Value.cbData,
             CRYPT_DECODE_ALLOC_FLAG,nullptr,&basic,&basic_size) && basic->fCA;
    if (basic) LocalFree(basic);
    DWORD length = 0;
    if (valid) valid = CryptBinaryToStringA(cert->pbCertEncoded,cert->cbCertEncoded,CRYPT_STRING_BASE64HEADER,nullptr,&length) != FALSE;
    if (valid) {
        pem.resize(length);
        valid = CryptBinaryToStringA(cert->pbCertEncoded,cert->cbCertEncoded,CRYPT_STRING_BASE64HEADER,pem.data(),&length) != FALSE;
        while (!pem.empty() && pem.back() == '\0') pem.pop_back();
    }
    CertFreeCertificateContext(cert);
    return valid;
}
static bool atomic_write(const std::wstring& path, const std::vector<BYTE>& bytes, DWORD& error) {
    auto temporary = path + L".new-" + std::to_wstring(GetCurrentProcessId());
    {
        Handle file(CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
        if (!file) { error=GetLastError(); return false; }
        DWORD written = 0;
        if (!WriteFile(file.get(),bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr) || written != bytes.size() || !FlushFileBuffers(file.get())) {
            error = GetLastError(); file.reset(); DeleteFileW(temporary.c_str()); return false;
        }
    }
    if (!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = GetLastError(); DeleteFileW(temporary.c_str()); return false;
    }
    return true;
}
static std::wstring pin_path(const std::wstring& server, const std::wstring& directory, DWORD& error) {
    auto origin = utf8(server_origin(server));
    if (origin.empty()) { error = ERROR_INVALID_PARAMETER; return {}; }
    auto root = directory;
    if (root.empty()) {
        auto settings = settings_path();
        if (settings.empty()) { error = ERROR_PATH_NOT_FOUND; return {}; }
        root = settings.substr(0, settings.find_last_of(L'\\'));
    }
    auto digest = sha256(std::vector<BYTE>(origin.begin(), origin.end()));
    if (digest.empty()) { error = ERROR_GEN_FAILURE; return {}; }
    return root + L"\\trusted-" + wide(digest) + L".ini";
}
bool read_server_pin(const std::wstring& server, std::string& pin, DWORD& error, const std::wstring& directory) {
    pin.clear(); error = ERROR_SUCCESS;
    auto path = pin_path(server, directory, error);
    if (path.empty()) return false;
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND) { error = ERROR_SUCCESS; return true; }
        return false;
    }
    wchar_t origin[1100]{}, value[128]{};
    GetPrivateProfileStringW(L"Server", L"Origin", L"", origin, 1100, path.c_str());
    GetPrivateProfileStringW(L"Server", L"Pin", L"", value, 128, path.c_str());
    pin = utf8(value);
    if (origin != server_origin(server) || pin.empty() || !valid_pin(pin)) { pin.clear(); error = ERROR_INVALID_DATA; return false; }
    return true;
}
bool save_server_pin(const std::wstring& server, const std::string& pin, DWORD& error, const std::wstring& directory) {
    error = ERROR_SUCCESS;
    if (pin.empty() || !valid_pin(pin)) { error = ERROR_INVALID_PARAMETER; return false; }
    auto path = pin_path(server, directory, error);
    if (path.empty()) return false;
    std::wstring contents = L"\xfeff[Server]\r\nOrigin=" + server_origin(server) + L"\r\nPin=" + wide(pin) + L"\r\n";
    auto first = reinterpret_cast<const BYTE*>(contents.data());
    if (!atomic_write(path, std::vector<BYTE>(first, first + contents.size() * sizeof(wchar_t)), error)) return false;
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
    return true;
}
bool select_server_profile(const Profile& configured, const std::wstring& server, Profile& selected, DWORD& error, const std::wstring& directory) {
    error = ERROR_SUCCESS;
    std::wstring normalized;
    if (!normalize_server(server, normalized)) { error = ERROR_INVALID_PARAMETER; return false; }
    selected = configured;
    if (server_origin(configured.server) != server_origin(normalized)) {
        selected.pin.clear(); selected.ca_file.clear(); selected.auth_group.clear();
        selected.remembered_pin = false;
    }
    selected.server = normalized;
    if (selected.pin.empty()) {
        if (!read_server_pin(normalized, selected.pin, error, directory)) return false;
        selected.remembered_pin = !selected.pin.empty();
        if (selected.remembered_pin) selected.ca_file.clear();
    }
    return true;
}
static bool valid_auth_group(const std::wstring& value) {
    return value.size() <= 256 && value == trim(value) &&
        std::none_of(value.begin(), value.end(), [](wchar_t c) { return c < 32 || c == 127 || c == L'\"'; });
}
static std::wstring connection_options(const Profile& profile) {
    return L"\r\nAuthGroup=" + wide(profile.auth_group) +
        L"\r\nPreferUDP=" + (profile.prefer_udp ? L"1" : L"0") +
        L"\r\nReconnectSeconds=" + std::to_wstring(profile.reconnect_seconds) +
        L"\r\nProtectDNS=" + (profile.protect_dns ? L"1" : L"0") +
        L"\r\nBlockUntunneledIPv6=" + (profile.block_ipv6 ? L"1" : L"0") + L"\r\n";
}
bool export_connection(const std::wstring& file, const Profile& profile, std::wstring& error) {
    error.clear();
    std::wstring normalized;
    if (!normalize_server(profile.server, normalized)) { error = L"请先填写有效的服务器地址。 / Enter a valid server address first."; return false; }
    if (!valid_pin(profile.pin) || (!profile.pin.empty() && !profile.ca_file.empty()) ||
        !valid_auth_group(wide(profile.auth_group)) || profile.reconnect_seconds < 0 || profile.reconnect_seconds > 300) {
        error = L"连接配置无效，无法导出。 / The connection profile is invalid."; return false;
    }
    std::wstring contents = L"\xfeff[VPN]\r\nServer=" + normalized;
    if (!profile.pin.empty()) contents += L"\r\nServerPin=" + wide(profile.pin);
    DWORD status = 0;
    if (!profile.ca_file.empty()) {
        std::vector<BYTE> bytes;
        std::string pem;
        if (!read_file(profile.ca_file, bytes, status)) { error = system_error(status); return false; }
        if (!certificate_pem(bytes, pem) || pem.size() > 49149) {
            error = L"只能导出有效的公共 CA 证书，不能包含私钥。 / Export requires a valid public CA certificate without a private key."; return false;
        }
        DWORD size = 0;
        auto data = reinterpret_cast<const BYTE*>(pem.data());
        const DWORD flags = CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF;
        if (!CryptBinaryToStringW(data, static_cast<DWORD>(pem.size()), flags, nullptr, &size)) { error = system_error(GetLastError()); return false; }
        std::vector<wchar_t> encoded(size);
        if (!CryptBinaryToStringW(data, static_cast<DWORD>(pem.size()), flags, encoded.data(), &size)) { error = system_error(GetLastError()); return false; }
        contents += L"\r\nCABase64=" + std::wstring(encoded.data());
    }
    contents += connection_options(profile);
    const auto* first = reinterpret_cast<const BYTE*>(contents.data());
    if (!atomic_write(file, std::vector<BYTE>(first, first + contents.size() * sizeof(wchar_t)), status)) { error = system_error(status); return false; }
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, file.c_str());
    return true;
}
bool import_connection(const std::wstring& file, const Profile& current, std::wstring& error, const std::wstring& destination) {
    DWORD status = 0;
    std::vector<BYTE> bytes;
    if (!read_file(file,bytes,status)) { error=system_error(status); return false; }
    std::wstring server = current.server;
    std::string pin, pem;
    Profile options;
    auto dot = file.find_last_of(L'.');
    bool bundle = dot != std::wstring::npos && _wcsicmp(file.substr(dot).c_str(),L".bvpn") == 0;
    if (bundle) {
        std::vector<wchar_t> value(65536);
        GetPrivateProfileStringW(L"VPN",L"Server",L"",value.data(),static_cast<DWORD>(value.size()),file.c_str());
        server = trim(value.data());
        GetPrivateProfileStringW(L"VPN",L"ServerPin",L"",value.data(),static_cast<DWORD>(value.size()),file.c_str());
        pin = utf8(trim(value.data()));
        GetPrivateProfileStringW(L"VPN",L"CABase64",L"",value.data(),static_cast<DWORD>(value.size()),file.c_str());
        std::wstring encoded = trim(value.data());
        if ((!pin.empty() && !encoded.empty()) || !valid_pin(pin)) {
            error = L"连接配置中的 CA 证书与 SHA-256 指纹不能同时设置，指纹必须完整。 / Use either a CA certificate or a complete SHA-256 pin.";
            return false;
        }
        if (!encoded.empty()) {
            DWORD size = 0;
            if (!CryptStringToBinaryW(encoded.c_str(),static_cast<DWORD>(encoded.size()),CRYPT_STRING_BASE64,nullptr,&size,nullptr,nullptr) || size > 49152) {
                error = L"无效的 CA 证书数据。 / Invalid CA certificate data."; return false;
            }
            bytes.resize(size);
            if (!CryptStringToBinaryW(encoded.c_str(),static_cast<DWORD>(encoded.size()),CRYPT_STRING_BASE64,bytes.data(),&size,nullptr,nullptr) || !certificate_pem(bytes,pem)) {
                error = L"请选择有效的 CA 根证书；不能导入私钥或过期证书。 / Choose a valid CA certificate, not a private key or expired certificate."; return false;
            }
        }
        GetPrivateProfileStringW(L"VPN",L"AuthGroup",L"",value.data(),static_cast<DWORD>(value.size()),file.c_str());
        if (!valid_auth_group(value.data())) { error = L"无效的登录组。 / Invalid authentication group."; return false; }
        options.auth_group = utf8(value.data());
        const struct { const wchar_t* key; bool* value; } flags[] = {
            {L"PreferUDP", &options.prefer_udp}, {L"ProtectDNS", &options.protect_dns}, {L"BlockUntunneledIPv6", &options.block_ipv6}
        };
        for (const auto& option : flags) {
            GetPrivateProfileStringW(L"VPN",option.key,L"1",value.data(),static_cast<DWORD>(value.size()),file.c_str());
            std::wstring flag = value.data();
            if (flag != L"0" && flag != L"1") { error = L"连接选项必须为 0 或 1。 / Connection flags must be 0 or 1."; return false; }
            *option.value = flag == L"1";
        }
        GetPrivateProfileStringW(L"VPN",L"ReconnectSeconds",L"300",value.data(),static_cast<DWORD>(value.size()),file.c_str());
        std::wstring seconds = value.data();
        if (seconds.empty() || seconds.size() > 3 || seconds.find_first_not_of(L"0123456789") != std::wstring::npos || std::stoi(seconds) > 300) {
            error = L"重连时间须为 0 到 300 秒。 / Reconnection time must be between 0 and 300 seconds."; return false;
        }
        options.reconnect_seconds = std::stoi(seconds);
    } else if (!certificate_pem(bytes,pem)) {
        error = L"请选择有效的 CA 根证书（PEM/DER）；不能导入私钥。 / Choose a valid CA certificate (PEM/DER), not a private key."; return false;
    }
    std::wstring normalized;
    if (!normalize_server(server,normalized)) { error = L"请先填写有效的服务器地址。 / Enter a valid server address first."; return false; }
    auto target = destination.empty() ? user_profile_path() : destination;
    if (target.empty()) { error=L"无法保存连接配置。 / Could not save the connection profile."; return false; }
    auto directory = target.substr(0,target.find_last_of(L'\\'));
    std::wstring ca_name;
    if (!pem.empty()) {
        std::vector<BYTE> ca(pem.begin(),pem.end());
        ca_name = L"ca-" + wide(sha256(ca)) + L".pem";
        if (!atomic_write(directory + L"\\" + ca_name,ca,status)) { error=system_error(status); return false; }
    }
    std::wstring contents = L"\xfeff[VPN]\r\nServer=" + normalized + L"\r\nServerPin=" + wide(pin) +
        L"\r\nCAFile=" + ca_name + connection_options(options);
    const auto* first = reinterpret_cast<const BYTE*>(contents.data());
    std::vector<BYTE> out(first,first + contents.size()*sizeof(wchar_t));
    if (!atomic_write(target,out,status)) { error=system_error(status); return false; }
    WritePrivateProfileStringW(nullptr,nullptr,nullptr,target.c_str()); // Invalidate the Windows INI cache.
    return true;
}
}
