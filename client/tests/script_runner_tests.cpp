// Exercise the patched official Windows Script Host launcher before any adapter is created.
#include "session.h"
#include <cerrno>
#include <iostream>
#include <stdexcept>

using namespace bulijie;
namespace {
void progress(void *, int, const char *, ...) {}
}

int wmain(int argc, wchar_t **argv) {
    if (argc != 2)
        return 2;
    SetDllDirectoryW(L"");
    WSADATA sockets{};
    if (WSAStartup(MAKEWORD(2, 2), &sockets) || openconnect_init_ssl())
        return 2;
    unsigned passed = 0;
    try {
        auto directory = std::filesystem::absolute(argv[1]) / L"脚本 runner" / wide(random_id());
        std::filesystem::create_directories(directory);
        for (const auto &fixture : {std::string("WScript.Quit(1);"), std::string("WScript.Quit(259);"),
                                    std::string("var env = WScript.CreateObject('WScript.Shell').Environment('Process');"
                                                "WScript.Quit(env('reason') === 'pre-init' ? 7 : 0);")}) {
            auto script = directory / (std::to_wstring(passed) + L".js");
            std::wstring error;
            if (!write_atomic(script, fixture, error))
                throw std::runtime_error("Could not write script fixture");
            openconnect_info *vpn = openconnect_vpninfo_new("script-fixture", nullptr, nullptr, nullptr,
                                                           progress, nullptr);
            if (!vpn)
                throw std::runtime_error("Could not create test context");
            int result = openconnect_setup_tun_device(vpn, utf8(script.wstring()).c_str(), "ScriptFixture");
            openconnect_vpninfo_free(vpn);
            if (result != -EIO)
                throw std::runtime_error("The pre-init script failure was not propagated");
            ++passed;
        }
        // This path cannot be opened: verify that the shipped helper converts a real
        // FileSystemObject exception to a failed pre-init, without reaching adapter setup.
        auto invalid_log = directory / L"missing-parent" / L"script.log";
        if (!SetEnvironmentVariableW(L"BULIJIE_SCRIPT_LOG", invalid_log.c_str()))
            throw std::runtime_error("Could not set the script fixture environment");
        openconnect_info *vpn = openconnect_vpninfo_new("script-fixture", nullptr, nullptr, nullptr,
                                                       progress, nullptr);
        if (!vpn)
            throw std::runtime_error("Could not create test context");
        auto shipped = executable_directory() / L"vpnc-script-win.js";
        int result = openconnect_setup_tun_device(vpn, utf8(shipped.wstring()).c_str(), "ScriptFixture");
        openconnect_vpninfo_free(vpn);
        if (result != -EIO)
            throw std::runtime_error("The shipped script did not propagate its exception");
        ++passed;
        std::cout << "{\"passed\":" << passed
                  << ",\"boundary\":\"Real Windows Script Host; failing pre-init fixtures create no adapters\"}\n";
        WSACleanup();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\n";
        WSACleanup();
        return 1;
    }
}
