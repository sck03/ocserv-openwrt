#include "application.h"
#include <shellapi.h>
#include <objbase.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int show) {
    using namespace bulijie;
    SetDllDirectoryW(L"");
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_TAB_CLASSES};
    InitCommonControlsEx(&controls);
    WSADATA sockets{};
    if (WSAStartup(MAKEWORD(2, 2), &sockets) != 0)
        return 1;
    HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int result = 1;
    try {
        if (openconnect_init_ssl() != 0)
            throw std::runtime_error("OpenConnect TLS initialization failed");
        int count = 0;
        wchar_t **arguments = CommandLineToArgvW(GetCommandLineW(), &count);
        std::filesystem::path data;
        std::string connect;
        bool valid = arguments != nullptr;
        for (int i = 1; i < count; ++i) {
            std::wstring argument = arguments[i];
            if (argument == L"--data-dir" && i + 1 < count)
                data = std::filesystem::absolute(arguments[++i]);
            else if (argument == L"--connect" && i + 1 < count)
                connect = utf8(arguments[++i]);
            else
                valid = false;
        }
        if (arguments)
            LocalFree(arguments);
        if (!valid)
            ui::error(nullptr,
                      L"用法 / Usage: 布利杰VPN.exe [--data-dir <directory>] [--connect <profile-id>]");
        else {
            Application application(std::move(data));
            result = application.run(show, connect);
        }
    } catch (const std::exception &exception) {
        ui::error(nullptr, L"客户端无法启动。 / Could not start the client.\n" + wide(exception.what()));
    } catch (...) {
        ui::error(nullptr, L"客户端无法启动。 / Could not start the client.");
    }
    if (SUCCEEDED(apartment))
        CoUninitialize();
    WSACleanup();
    return result;
}
