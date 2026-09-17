#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace bulijie {
inline constexpr wchar_t Product[] = L"布利杰VPN";
inline constexpr char Version[] = "0.5.0";
enum class Language { Chinese, English };
inline const wchar_t *tr(Language language, const wchar_t *zh, const wchar_t *en) {
    return language == Language::Chinese ? zh : en;
}
class Handle {
public:
    explicit Handle(HANDLE value = nullptr) : value_(value) {}
    ~Handle() {
        reset();
    }
    Handle(Handle &&other) noexcept : value_(other.release()) {}
    Handle &operator=(Handle &&other) noexcept {
        if (this != &other)
            reset(other.release());
        return *this;
    }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    HANDLE get() const {
        return value_;
    }
    explicit operator bool() const {
        return value_ && value_ != INVALID_HANDLE_VALUE;
    }
    HANDLE release() {
        HANDLE value = value_;
        value_ = nullptr;
        return value;
    }
    void reset(HANDLE value = nullptr) {
        if (*this)
            CloseHandle(value_);
        value_ = value;
    }

private:
    HANDLE value_;
};
std::string utf8(const std::wstring &value);
std::wstring wide(const std::string &value);
std::wstring trim(const std::wstring &value);
std::wstring system_error(DWORD code);
std::filesystem::path executable_directory();
std::string random_id();
bool read_file(const std::filesystem::path &path, std::string &value, std::wstring &error,
               size_t limit = 8 * 1024 * 1024);
bool write_atomic(const std::filesystem::path &path, const std::string &value, std::wstring &error);
std::string base64(const std::string &value);
bool unbase64(const std::string &value, std::string &decoded);
bool protect_secret(const std::string &value, const std::string &context, std::string &encoded,
                    std::wstring &error);
bool unprotect_secret(const std::string &encoded, const std::string &context, std::string &value);
void erase(std::string &value);
bool normalize_gateway(const std::wstring &input, std::wstring &normalized, std::wstring *origin = nullptr);
bool valid_pin(const std::string &pin);
bool valid_public_certificates(const std::string &raw, std::wstring &error);
bool administrator();
std::wstring read_window_text(HWND window, size_t limit = 32768);
void copy_text(HWND owner, const std::wstring &value);
} // namespace bulijie
