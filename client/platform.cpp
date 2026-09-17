#include "platform.h"
#include <bcrypt.h>
#include <wincrypt.h>
#include <winhttp.h>
#include <algorithm>
#include <climits>
#include <cwctype>

namespace bulijie {
std::string utf8(const std::wstring &value) {
    if (value.empty())
        return {};
    if (value.size() > INT_MAX)
        return {};
    int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                    static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0)
        return {};
    std::string result(static_cast<size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                        result.data(), count, nullptr, nullptr);
    return result;
}
std::wstring wide(const std::string &value) {
    if (value.empty())
        return {};
    if (value.size() > INT_MAX)
        return {};
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                    static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0)
        return L"[invalid UTF-8]";
    std::wstring result(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                        result.data(), count);
    return result;
}
std::wstring trim(const std::wstring &value) {
    size_t first = value.find_first_not_of(L" \t\r\n");
    return first == std::wstring::npos ? L""
                                       : value.substr(first, value.find_last_not_of(L" \t\r\n") - first + 1);
}
std::wstring system_error(DWORD code) {
    wchar_t *buffer = nullptr;
    DWORD count = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                     FORMAT_MESSAGE_IGNORE_INSERTS,
                                 nullptr, code, 0, reinterpret_cast<wchar_t *>(&buffer), 0, nullptr);
    std::wstring result =
        count && buffer ? trim(std::wstring(buffer, count)) : L"Windows error " + std::to_wstring(code);
    if (buffer)
        LocalFree(buffer);
    return result;
}
std::filesystem::path executable_directory() {
    std::vector<wchar_t> path(32768);
    DWORD count = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    return count && count < path.size()
               ? std::filesystem::path(std::wstring(path.data(), count)).parent_path()
               : std::filesystem::path();
}
std::string random_id() {
    unsigned char bytes[16]{};
    if (BCryptGenRandom(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        return {};
    constexpr char alphabet[] = "0123456789abcdef";
    std::string result;
    for (unsigned char value : bytes) {
        result += alphabet[value >> 4];
        result += alphabet[value & 15];
    }
    return result;
}
bool read_file(const std::filesystem::path &path, std::string &value, std::wstring &error, size_t limit) {
    value.clear();
    Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file) {
        error = system_error(GetLastError());
        return false;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.get(), &size) || size.QuadPart < 0 ||
        static_cast<uint64_t>(size.QuadPart) > limit) {
        error = L"File exceeds the supported size.";
        return false;
    }
    value.resize(static_cast<size_t>(size.QuadPart));
    DWORD count = 0;
    if (!value.empty() &&
        (!ReadFile(file.get(), value.data(), static_cast<DWORD>(value.size()), &count, nullptr) ||
         count != value.size())) {
        error = system_error(GetLastError());
        value.clear();
        return false;
    }
    return true;
}
bool write_atomic(const std::filesystem::path &path, const std::string &value, std::wstring &error) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        error = wide(ec.message());
        return false;
    }
    std::string suffix = random_id();
    if (suffix.empty()) {
        error = L"Could not generate a temporary file name.";
        return false;
    }
    auto temporary = path;
    temporary += L".new-" + wide(suffix);
    Handle file(CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr));
    if (!file) {
        error = system_error(GetLastError());
        return false;
    }
    DWORD count = 0;
    bool ok = value.size() <= MAXDWORD &&
              WriteFile(file.get(), value.data(), static_cast<DWORD>(value.size()), &count, nullptr) &&
              count == value.size() && FlushFileBuffers(file.get());
    DWORD code = ok ? ERROR_SUCCESS : GetLastError();
    file.reset();
    if (ok &&
        !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        ok = false;
        code = GetLastError();
    }
    if (!ok) {
        DeleteFileW(temporary.c_str());
        error = system_error(code);
    }
    return ok;
}
std::string base64(const std::string &value) {
    if (value.empty())
        return {};
    if (value.size() > MAXDWORD)
        return {};
    DWORD length = 0;
    if (!CryptBinaryToStringA(reinterpret_cast<const BYTE *>(value.data()), static_cast<DWORD>(value.size()),
                              CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &length))
        return {};
    std::string result(length, '\0');
    if (!CryptBinaryToStringA(reinterpret_cast<const BYTE *>(value.data()), static_cast<DWORD>(value.size()),
                              CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, result.data(), &length))
        return {};
    result.resize(length);
    while (!result.empty() && result.back() == '\0')
        result.pop_back();
    return result;
}
bool unbase64(const std::string &value, std::string &decoded) {
    decoded.clear();
    if (value.empty())
        return true;
    if (value.size() > 16 * 1024 * 1024)
        return false;
    DWORD size = 0;
    if (!CryptStringToBinaryA(value.data(), static_cast<DWORD>(value.size()),
                              CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT, nullptr, &size, nullptr, nullptr))
        return false;
    decoded.resize(size);
    return CryptStringToBinaryA(value.data(), static_cast<DWORD>(value.size()),
                                CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT,
                                reinterpret_cast<BYTE *>(decoded.data()), &size, nullptr, nullptr) != FALSE;
}
bool protect_secret(const std::string &value, const std::string &context, std::string &encoded,
                    std::wstring &error) {
    encoded.clear();
    if (value.empty())
        return true;
    if (value.size() > MAXDWORD || context.size() > MAXDWORD)
        return false;
    DATA_BLOB input{static_cast<DWORD>(value.size()),
                    reinterpret_cast<BYTE *>(const_cast<char *>(value.data()))};
    DATA_BLOB entropy{static_cast<DWORD>(context.size()),
                      reinterpret_cast<BYTE *>(const_cast<char *>(context.data()))};
    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"BulijieVPN profile", &entropy, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        error = system_error(GetLastError());
        return false;
    }
    encoded = base64(std::string(reinterpret_cast<char *>(output.pbData), output.cbData));
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return !encoded.empty();
}
bool unprotect_secret(const std::string &encoded, const std::string &context, std::string &value) {
    value.clear();
    if (encoded.empty())
        return true;
    std::string encrypted;
    if (!unbase64(encoded, encrypted))
        return false;
    DATA_BLOB input{static_cast<DWORD>(encrypted.size()), reinterpret_cast<BYTE *>(encrypted.data())};
    DATA_BLOB entropy{static_cast<DWORD>(context.size()),
                      reinterpret_cast<BYTE *>(const_cast<char *>(context.data()))};
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output))
        return false;
    value.assign(reinterpret_cast<char *>(output.pbData), output.cbData);
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return true;
}
void erase(std::string &value) {
    if (!value.empty())
        SecureZeroMemory(value.data(), value.size());
    value.clear();
}
bool normalize_gateway(const std::wstring &input, std::wstring &normalized, std::wstring *origin) {
    normalized = trim(input);
    if (normalized.empty() || normalized.size() > 4096)
        return false;
    for (wchar_t c : normalized)
        if (c < 33 || c == 127 || c == L'\\' || c == L'"' || c == L'#')
            return false;
    if (normalized.find(L"://") == std::wstring::npos)
        normalized = L"https://" + normalized;
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = parts.dwUserNameLength =
        parts.dwPasswordLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(normalized.c_str(), static_cast<DWORD>(normalized.size()), 0, &parts) ||
        parts.nScheme != INTERNET_SCHEME_HTTPS || !parts.dwHostNameLength || !parts.nPort ||
        parts.dwUserNameLength || parts.dwPasswordLength)
        return false;
    std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    if (host.find_first_of(L" /\\\"'@%") != std::wstring::npos)
        return false;
    std::transform(host.begin(), host.end(), host.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    if (host.find(L':') != std::wstring::npos) {
        auto address=host;
        if(address.front()==L'['&&address.back()==L']')address=address.substr(1,address.size()-2);
        IN6_ADDR ipv6{};
        if(InetPtonW(AF_INET6,address.c_str(),&ipv6)!=1)return false;
        host=L"["+address+L"]";
    } else {
        if(std::any_of(host.begin(),host.end(),[](wchar_t c){return c>127;})) {
            int length=IdnToAscii(IDN_USE_STD3_ASCII_RULES,host.data(),static_cast<int>(host.size()),nullptr,0);
            if(length<=0)return false;
            std::wstring ascii(static_cast<size_t>(length),L'\0');
            if(!IdnToAscii(IDN_USE_STD3_ASCII_RULES,host.data(),static_cast<int>(host.size()),ascii.data(),length))return false;
            host=std::move(ascii);
        }
        if(!std::all_of(host.begin(),host.end(),[](wchar_t c){return (c>=L'a'&&c<=L'z')||(c>=L'0'&&c<=L'9')||c==L'-'||c==L'.'||c==L'_';}))return false;
    }
    if (origin)
        *origin = L"https://" + host + L":" + std::to_wstring(parts.nPort);
    std::wstring path=parts.dwUrlPathLength?std::wstring(parts.lpszUrlPath,parts.dwUrlPathLength):L"";
    std::wstring extra=parts.dwExtraInfoLength?std::wstring(parts.lpszExtraInfo,parts.dwExtraInfoLength):L"";
    normalized=L"https://"+host+(parts.nPort==443?L"":L":"+std::to_wstring(parts.nPort))+path+extra;
    return true;
}
bool valid_pin(const std::string &value) {
    if (value.empty())
        return true;
    if (value.rfind("pin-sha256:", 0) != 0 || value.size() != 55 || value.back() != '=')
        return false;
    for (size_t i = 11; i + 1 < value.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(value[i]);
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' ||
              c == '/'))
            return false;
    }
    std::string decoded;
    return unbase64(value.substr(11), decoded) && decoded.size() == 32 && base64(decoded) == value.substr(11);
}
bool valid_public_certificates(const std::string &raw, std::wstring &error) {
    if (raw.empty() || raw.size() > 1024 * 1024 || raw.find("PRIVATE KEY") != std::string::npos) {
        error = L"CA 文件必须只包含公共证书。 / A CA file must contain only public certificates.";
        return false;
    }
    auto valid_der = [](const BYTE *data, DWORD size) {
        PCCERT_CONTEXT certificate = CertCreateCertificateContext(X509_ASN_ENCODING, data, size);
        if (!certificate)
            return false;
        CertFreeCertificateContext(certificate);
        return true;
    };
    const std::string begin = "-----BEGIN CERTIFICATE-----", end = "-----END CERTIFICATE-----";
    size_t position = 0;
    unsigned count = 0;
    while ((position = raw.find(begin, position)) != std::string::npos) {
        auto close = raw.find(end, position + begin.size());
        if (close == std::string::npos || ++count > 64)
            break;
        DWORD size = 0;
        const char *block = raw.data() + position;
        DWORD length = static_cast<DWORD>(close + end.size() - position);
        if (!CryptStringToBinaryA(block, length, CRYPT_STRING_BASE64HEADER, nullptr, &size, nullptr, nullptr))
            break;
        std::vector<BYTE> der(size);
        if (!CryptStringToBinaryA(block, length, CRYPT_STRING_BASE64HEADER, der.data(), &size, nullptr,
                                  nullptr) ||
            !valid_der(der.data(), size))
            break;
        position = close + end.size();
        if (raw.find(begin, position) == std::string::npos)
            return true;
    }
    if (!count && raw.find("-----BEGIN") == std::string::npos &&
        valid_der(reinterpret_cast<const BYTE *>(raw.data()), static_cast<DWORD>(raw.size())))
        return true;
    error = L"CA 证书内容无效。 / Invalid CA certificate contents.";
    return false;
}
bool administrator() {
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
    PSID sid = nullptr;
    BOOL member = FALSE;
    if (AllocateAndInitializeSid(&authority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0,
                                 0, 0, 0, &sid)) {
        CheckTokenMembership(nullptr, sid, &member);
        FreeSid(sid);
    }
    return member != FALSE;
}
std::wstring read_window_text(HWND window, size_t limit) {
    size_t length = std::min(static_cast<size_t>(std::max(GetWindowTextLengthW(window), 0)), limit);
    std::wstring value(length + 1, L'\0');
    GetWindowTextW(window, value.data(), static_cast<int>(value.size()));
    value.resize(wcslen(value.c_str()));
    return value;
}
void copy_text(HWND owner, const std::wstring &value) {
    if (!OpenClipboard(owner))
        return;
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (value.size() + 1) * sizeof(wchar_t));
    if (memory) {
        void *data = GlobalLock(memory);
        if (data) {
            memcpy(data, value.c_str(), (value.size() + 1) * sizeof(wchar_t));
            GlobalUnlock(memory);
            EmptyClipboard();
            if (!SetClipboardData(CF_UNICODETEXT, memory))
                GlobalFree(memory);
        } else
            GlobalFree(memory);
    }
    CloseClipboard();
}
} // namespace bulijie
